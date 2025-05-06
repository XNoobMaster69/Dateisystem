#include <grpcpp/grpcpp.h>
#include "./generated/dateisystem.pb.h"
#include "./generated/dateisystem.grpc.pb.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using namespace dateisystem;
using grpc::Channel;
using grpc::ClientContext;

class SyncClient {
  std::unique_ptr<PrimaryService::Stub> stub_;
public:
  SyncClient(std::shared_ptr<Channel> ch)
    : stub_(PrimaryService::NewStub(ch)) {}

  bool SyncFile(const std::string& rel, const std::string& content) {
    SyncRequest rq; rq.set_file_path(rel); rq.set_file_content(content);
    SyncResponse rs; ClientContext ctx;
    return stub_->SyncFile(&ctx, rq, &rs).ok() && rs.success();
  }

  bool DeleteFile(const std::string& rel) {
    DeleteRequest rq; rq.set_file_path(rel);
    DeleteResponse rs; ClientContext ctx;
    return stub_->DeleteFile(&ctx, rq, &rs).ok() && rs.success();
  }

  std::vector<FileEntry> ListFiles() {
    ListRequest rq; ListResponse rs; ClientContext ctx;
    stub_->ListFiles(&ctx, rq, &rs);
    return { rs.entries().begin(), rs.entries().end() };
  }
};

static std::string fileHash(const std::filesystem::path& p) {
  try {
    auto s = std::filesystem::file_size(p);
    auto t = std::filesystem::last_write_time(p).time_since_epoch().count();
    return std::to_string(s) + "_" + std::to_string(t);
  } catch(...) {
    return "";
  }
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0]
              << " <server:port> <directory>\n";
    return 1;
  }
  std::string server_addr = argv[1];
  std::filesystem::path root = argv[2];
  if (!std::filesystem::is_directory(root)) {
    std::cerr << "Not a directory: " << root << "\n";
    return 1;
  }

  // Wir nehmen den Ordnernamen als prefix
  std::string prefix = root.filename().string() + "/";

  SyncClient client(
    grpc::CreateChannel(server_addr, grpc::InsecureChannelCredentials())
  );

  // 1) Initial Pull aller Dateien vom Server
  {
    auto srv = client.ListFiles();
    for (auto& fe : srv) {
      const auto& path = fe.file_path();
      if (path.rfind(prefix, 0) != 0) continue;
      auto rel = path.substr(prefix.size());
      auto full = root / rel;
      std::filesystem::create_directories(full.parent_path());
      if (!std::filesystem::exists(full)) {
        std::ofstream out(full, std::ios::binary);
        out.write(fe.file_content().data(), fe.file_content().size());
      }
    }
  }

  // 2) Initial: alle lokalen Dateien pushen
  std::unordered_set<std::string> last_local;
  std::unordered_map<std::string,std::string> last_hash;
  for (auto& ent : std::filesystem::recursive_directory_iterator(root)) {
    if (!ent.is_regular_file()) continue;
    auto rel0 = std::filesystem::relative(ent.path(), root).string();
    std::string key = prefix + rel0;
    last_local.insert(key);
    last_hash[key] = fileHash(ent.path());
    std::ifstream in(ent.path(), std::ios::binary);
    std::string buf{ std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>() };
    client.SyncFile(key, buf);
  }

  // 3) Polling-Loop
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // A) Scan lokal: cur_local und cur_hash
    std::unordered_set<std::string> cur_local;
    std::unordered_map<std::string,std::string> cur_hash;
    for (auto& ent : std::filesystem::recursive_directory_iterator(root)) {
      if (!ent.is_regular_file()) continue;
      auto rel0 = std::filesystem::relative(ent.path(), root).string();
      std::string key = prefix + rel0;
      cur_local.insert(key);
      cur_hash[key] = fileHash(ent.path());
    }

    // B) Neue/Geänderte lokal → SyncFile
    for (auto& key : cur_local) {
      bool is_new = !last_local.count(key);
      bool modified = last_hash.count(key) && last_hash[key] != cur_hash[key];
      if (is_new || modified) {
        auto full = root / key.substr(prefix.size());
        std::ifstream in(full, std::ios::binary);
        std::string buf{ std::istreambuf_iterator<char>(in),
                         std::istreambuf_iterator<char>() };
        std::cout << "→ SyncFile: " << key << "\n";
        client.SyncFile(key, buf);
      }
    }

    // C) Lokale Löschungen → DeleteFile
    for (auto& key : last_local) {
      if (!cur_local.count(key)) {
        std::cout << "→ DeleteFile: " << key << "\n";
        client.DeleteFile(key);
      }
    }

    // D) Server-Snapshot holen
    auto srv = client.ListFiles();
    std::unordered_set<std::string> srv_set;
    std::unordered_map<std::string,std::string> srv_content;
    for (auto& fe : srv) {
      const auto& p = fe.file_path();
      if (p.rfind(prefix,0)!=0) continue;
      srv_set.insert(p);
      srv_content[p] = fe.file_content();
    }

    // E) Server-Löschungen → lokal entfernen
    for (auto& key : last_local) {
      if (srv_set.count(key)==0 && cur_local.count(key)) {
        auto full = root / key.substr(prefix.size());
        std::filesystem::remove(full);
        std::cout << "→ Local delete: " << key << "\n";
      }
    }

    // F) Neue Server-Dateien → lokal anlegen
    for (auto& key : srv_set) {
      if (!cur_local.count(key)) {
        auto rel = key.substr(prefix.size());
        auto full = root / rel;
        std::filesystem::create_directories(full.parent_path());
        std::ofstream out(full, std::ios::binary);
        auto& buf = srv_content[key];
        out.write(buf.data(), buf.size());
        std::cout << "→ Pulled new: " << key << "\n";
      }
    }

    // G) Update last_local & last_hash
    last_local = std::move(cur_local);
    last_hash  = std::move(cur_hash);
  }

  return 0;
}
