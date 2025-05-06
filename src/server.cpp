#include <grpcpp/grpcpp.h>
#include "./generated/dateisystem.pb.h"
#include "./generated/dateisystem.grpc.pb.h"

#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <vector>
#include <map>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <system_error>

using namespace dateisystem;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ClientContext;
using grpc::Status;

// Basis-Verzeichnis für alle Knoten
static const std::filesystem::path DATA_DIR =
    std::filesystem::path(std::getenv("HOME")) / "data";

// Globaler Zustand
struct State {
  std::atomic<int64_t> next_seq{1};
  std::vector<LogEntry> log;
  std::map<int64_t, LogEntry> buffer;
  std::vector<std::string> peers;
  std::atomic<int64_t> clock_offset_ms{0};
  std::mutex mtx, peers_mtx;
};

// Hilfsfunktion: baut Snapshot des DATA_DIR für ListFiles()
static void snapshotDirectory(ListResponse* resp) {
  namespace fs = std::filesystem;
  for (auto& e : fs::recursive_directory_iterator(DATA_DIR)) {
    if (!e.is_regular_file()) continue;
    auto rel = fs::relative(e.path(), DATA_DIR).string();
    std::ifstream in(e.path(), std::ios::binary);
    std::string content{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    in.close();
    auto* fe = resp->add_entries();
    fe->set_file_path(rel);
    fe->set_file_content(content);
  }
}

// PrimaryService: schreibt lokal und repliziert an alle Peers
class PrimaryServiceImpl final : public PrimaryService::Service {
  State& S_;
public:
  PrimaryServiceImpl(State& S) : S_(S) {}

  Status SyncFile(ServerContext*, const SyncRequest* req, SyncResponse* resp) override {
    namespace fs = std::filesystem;
    // Zielpfad erzeugen
    fs::path target = DATA_DIR / req->file_path();
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (ec) {
      resp->set_success(false);
      resp->set_message("mkdir failed: " + ec.message());
      return Status::OK;
    }
    // Datei lokal schreiben
    {
      std::ofstream out(target, std::ios::binary);
      out.write(req->file_content().data(), req->file_content().size());
    }
    // Log-Eintrag anlegen
    int64_t seq = S_.next_seq.fetch_add(1);
    int64_t ts  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch()
                 ).count()
                 + S_.clock_offset_ms.load();
    LogEntry entry;
    entry.set_seq(seq);
    entry.set_timestamp(ts);
    entry.set_file_path(req->file_path());
    entry.set_file_content(req->file_content());
    entry.set_is_delete(false);
    {
      std::lock_guard<std::mutex> lk(S_.mtx);
      S_.log.push_back(entry);
    }
    // Replikation an Peers (Quorum = all)
    std::vector<std::string> peers;
    { std::lock_guard<std::mutex> lk(S_.peers_mtx); peers = S_.peers; }
    std::atomic<size_t> acks{0};
    std::vector<std::thread> thr;
    for (auto& addr : peers) {
      thr.emplace_back([&, addr]() {
        Ack ack; ClientContext ctx;
        auto stub = ReplicationService::NewStub(
          grpc::CreateChannel(addr, grpc::InsecureChannelCredentials()));
        if (stub->ReplicateEntry(&ctx, entry, &ack).ok() && ack.success())
          acks.fetch_add(1, std::memory_order_relaxed);
      });
    }
    for (auto& t : thr) t.join();

    bool ok = (acks.load() >= peers.size());
    resp->set_success(ok);
    resp->set_message(ok ? "synced" : "replication error");
    return Status::OK;
  }

  Status DeleteFile(ServerContext*, const DeleteRequest* req, DeleteResponse* resp) override {
    namespace fs = std::filesystem;
    // Datei löschen
    fs::path target = DATA_DIR / req->file_path();
    std::error_code ec;
    fs::remove(target, ec);
    if (ec) {
      resp->set_success(false);
      resp->set_message("remove failed: " + ec.message());
      return Status::OK;
    }
    // Log-Eintrag für Delete
    int64_t seq = S_.next_seq.fetch_add(1);
    int64_t ts  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch()
                 ).count()
                 + S_.clock_offset_ms.load();
    LogEntry entry;
    entry.set_seq(seq);
    entry.set_timestamp(ts);
    entry.set_file_path(req->file_path());
    entry.set_is_delete(true);
    {
      std::lock_guard<std::mutex> lk(S_.mtx);
      S_.log.push_back(entry);
    }
    // Replikation an Peers
    std::vector<std::string> peers;
    { std::lock_guard<std::mutex> lk(S_.peers_mtx); peers = S_.peers; }
    std::atomic<size_t> acks{0};
    std::vector<std::thread> thr;
    for (auto& addr : peers) {
      thr.emplace_back([&, addr]() {
        Ack ack; ClientContext ctx;
        auto stub = ReplicationService::NewStub(
          grpc::CreateChannel(addr, grpc::InsecureChannelCredentials()));
        if (stub->ReplicateEntry(&ctx, entry, &ack).ok() && ack.success())
          acks.fetch_add(1, std::memory_order_relaxed);
      });
    }
    for (auto& t : thr) t.join();

    bool ok = (acks.load() >= peers.size());
    resp->set_success(ok);
    resp->set_message(ok ? "deleted" : "del:replication error");
    return Status::OK;
  }

  Status ListFiles(ServerContext*, const ListRequest*, ListResponse* resp) override {
    snapshotDirectory(resp);
    return Status::OK;
  }
};

// ReplicationService: verwendet LogEntry auf lokalen DATA_DIR an
class ReplicationServiceImpl final : public ReplicationService::Service {
  State& S_;
public:
  ReplicationServiceImpl(State& S) : S_(S) {}

  Status ReplicateEntry(ServerContext*, const LogEntry* e, Ack* a) override {
    std::lock_guard<std::mutex> lk(S_.mtx);
    S_.buffer[e->seq()] = *e;
    int64_t want = S_.next_seq.load();
    namespace fs = std::filesystem;
    while (true) {
      auto it = S_.buffer.find(want);
      if (it == S_.buffer.end()) break;
      fs::path target = DATA_DIR / it->second.file_path();
      std::error_code ec;
      fs::create_directories(target.parent_path(), ec);
      if (it->second.is_delete()) {
        fs::remove(target, ec);
      } else {
        std::ofstream out(target, std::ios::binary);
        out.write(it->second.file_content().data(),
                  it->second.file_content().size());
      }
      S_.log.push_back(it->second);
      S_.buffer.erase(it);
      S_.next_seq.fetch_add(1);
      want++;
    }
    a->set_success(true);
    return Status::OK;
  }

  Status GetUpdates(ServerContext*, const UpdateRequest* req, UpdateResponse* resp) override {
    std::lock_guard<std::mutex> lk(S_.mtx);
    for (auto& e : S_.log) {
      if (e.seq() >= req->from_seq())
        *resp->add_entries() = e;
    }
    return Status::OK;
  }
};

// DiscoveryService: Join + Gossip
class DiscoveryServiceImpl final : public DiscoveryService::Service {
  State& S_;
public:
  DiscoveryServiceImpl(State& S) : S_(S) {}

  Status Join(ServerContext*, const JoinRequest* req, PeerList* out) override {
    {
      std::lock_guard<std::mutex> lk(S_.peers_mtx);
      if (std::find(S_.peers.begin(), S_.peers.end(), req->address()) == S_.peers.end())
        S_.peers.push_back(req->address());
    }
    {
      std::lock_guard<std::mutex> lk(S_.peers_mtx);
      std::cout << "[Join] Neuer Peer: " << req->address() << " → Peers:";
      for (auto& p : S_.peers) std::cout << " " << p;
      std::cout << "\n";
    }
    for (auto& p : S_.peers) out->add_peers(p);
    return Status::OK;
  }

  Status PeerExchange(ServerContext*, const PeerList* in, PeerList* out) override {
    {
      std::lock_guard<std::mutex> lk(S_.peers_mtx);
      for (auto& p : in->peers())
        if (std::find(S_.peers.begin(), S_.peers.end(), p) == S_.peers.end())
          S_.peers.push_back(p);
    }
    {
      std::lock_guard<std::mutex> lk(S_.peers_mtx);
      std::cout << "[PeerExchange] Eingehende Liste:";
      for (auto& p : in->peers()) std::cout << " " << p;
      std::cout << " → Peers jetzt:";
      for (auto& p : S_.peers) std::cout << " " << p;
      std::cout << "\n";
    }
    for (auto& p : S_.peers) out->add_peers(p);
    return Status::OK;
  }
};

// ClockSyncService: einfacher Berkeley-Algorithmus
class ClockSyncServiceImpl final : public ClockSyncService::Service {
  State& S_;
public:
  ClockSyncServiceImpl(State& S) : S_(S) {}

  Status GetTime(ServerContext*, const TimeRequest*, TimeResponse* resp) override {
    int64_t phys = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch()
                   ).count();
    resp->set_unix_millis(phys + S_.clock_offset_ms.load());
    return Status::OK;
  }

  Status AdjustTime(ServerContext*, const AdjustRequest* req, AdjustResponse* resp) override {
    S_.clock_offset_ms.fetch_add(req->offset_millis());
    resp->set_success(true);
    return Status::OK;
  }
};

int main(int argc, char** argv) {
  // ~/data anlegen
  std::error_code ec;
  std::filesystem::create_directories(DATA_DIR, ec);
  if (ec) {
    std::cerr << "mkdir " << DATA_DIR << " failed: " << ec.message() << "\n";
    return 1;
  }

  State S;
  bool is_master = (argc == 1);
  std::string master_addr, self_addr;
  if (is_master) {
    self_addr = "192.168.0.180:50051";  // eigene Adresse hardcodiert muss angepasst werden falls auf einem anderen System
    // Master startet mit leerer Peer-Liste
  } else {
    master_addr = argv[1];
    self_addr   = argv[2];
    std::lock_guard<std::mutex> lk(S.peers_mtx);
    S.peers = { self_addr };
  }

  // Falls Slave: Join am Master
  if (!is_master) {
    auto join_stub = DiscoveryService::NewStub(
      grpc::CreateChannel(master_addr, grpc::InsecureChannelCredentials())
    );
    JoinRequest jr; jr.set_address(self_addr);
    PeerList initial; ClientContext ctx;
    if (join_stub->Join(&ctx, jr, &initial).ok()) {
      std::cout << "[Bootstrap] Join erfolgreich, initial peers:";
      for (auto& p : initial.peers()) std::cout << " " << p;
      std::cout << "\n";
      std::lock_guard<std::mutex> lk(S.peers_mtx);
      S.peers.assign(initial.peers().begin(), initial.peers().end());
    } else {
      std::cerr << "Join RPC failed: " << ctx.debug_error_string() << "\n";
    }
  }

  // Stubs erst nach Join erzeugen
  std::vector<std::shared_ptr<ReplicationService::Stub>> repl_stubs;
  std::vector<std::shared_ptr<ClockSyncService::Stub>> clock_stubs;
  {
    std::lock_guard<std::mutex> lk(S.peers_mtx);
    for (auto& addr : S.peers) {
      repl_stubs.push_back(
        ReplicationService::NewStub(
          grpc::CreateChannel(addr, grpc::InsecureChannelCredentials())));
      clock_stubs.push_back(
        ClockSyncService::NewStub(
          grpc::CreateChannel(addr, grpc::InsecureChannelCredentials())));
    }
  }

  // Services registrieren
  auto primary_service   = std::make_unique<PrimaryServiceImpl>(S);
  ReplicationServiceImpl replication_service(S);
  DiscoveryServiceImpl   discovery_service(S);
  ClockSyncServiceImpl   clock_service(S);

  ServerBuilder builder;
  //builder.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
  builder.AddListeningPort("[::]:50051", grpc::InsecureServerCredentials());
  builder.RegisterService(primary_service.get());
  builder.RegisterService(&replication_service);
  builder.RegisterService(&discovery_service);
  builder.RegisterService(&clock_service);

  auto server = builder.BuildAndStart();
  std::cout << "Server läuft auf 0.0.0.0:50051\n";

  // Hintergrund-Thread: Anti-Entropy, Gossip, ClockSync
  std::thread([&]() {
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(10));

      // --- Anti-Entropy ---
      int64_t from = S.next_seq.load();
      std::vector<std::string> peers;
      { std::lock_guard<std::mutex> lk(S.peers_mtx); peers = S.peers; }
      for (auto& addr : peers) {
        auto stub = ReplicationService::NewStub(
          grpc::CreateChannel(addr, grpc::InsecureChannelCredentials()));
        UpdateRequest ur; ur.set_from_seq(from);
        UpdateResponse ursp; ClientContext ctx;
        if (stub->GetUpdates(&ctx, ur, &ursp).ok()) {
          std::lock_guard<std::mutex> lk2(S.mtx);
          for (auto& e : ursp.entries()) {
            S.buffer[e.seq()] = e;
          }
        }
      }

      // Gossip (PeerExchange)
      PeerList req;
      { std::lock_guard<std::mutex> lk(S.peers_mtx);
        for (auto& p : S.peers) req.add_peers(p);
      }
      for (auto& addr : peers) {
        auto stub = DiscoveryService::NewStub(
          grpc::CreateChannel(addr, grpc::InsecureChannelCredentials()));
        PeerList presp; ClientContext ctx;
        if (stub->PeerExchange(&ctx, req, &presp).ok()) {
          std::lock_guard<std::mutex> lk(S.peers_mtx);
          for (auto& np : presp.peers())
            if (std::find(S.peers.begin(), S.peers.end(), np) == S.peers.end())
              S.peers.push_back(np);
        }
      }

      // ClockSync (Master only)
      if (is_master) {
        std::vector<int64_t> times;
        times.push_back(
          std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
          ).count()
          + S.clock_offset_ms.load()
        );
        for (auto& stub : clock_stubs) {
          TimeResponse tresp; ClientContext ctx;
          if (stub->GetTime(&ctx, TimeRequest(), &tresp).ok())
            times.push_back(tresp.unix_millis());
        }
        int64_t sum = 0;
        for (auto t : times) sum += t;
        int64_t avg = sum / times.size();
        // verteile Anpassungen
        for (size_t i = 0; i < clock_stubs.size(); ++i) {
          int64_t delta = avg - times[i + 1];
          AdjustRequest ar; ar.set_offset_millis(delta);
          AdjustResponse arsp; ClientContext ctx;
          clock_stubs[i]->AdjustTime(&ctx, ar, &arsp);
        }
        int64_t self_delta = avg - times[0];
        S.clock_offset_ms.fetch_add(self_delta);
      }
    }
  }).detach();

  server->Wait();
  return 0;
}
