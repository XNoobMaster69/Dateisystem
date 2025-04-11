#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "./generated/dateisystem.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using dateisystem::SyncService;
using dateisystem::SyncRequest;
using dateisystem::SyncResponse;

class SyncServiceImpl final : public SyncService::Service {
  public:
      // RPC-Methode SyncFile (definiert in .proto)
      Status SyncFile(ServerContext* context, const SyncRequest* request, SyncResponse* reply) override {
          std::string file_path = request->file_path();
          std::string hash = request->hash();
          std::cout << "Sync-request for: " << file_path << " Hash: " << hash << std::endl;
          // Hier kommt die Logik für Replikation, Master-Slave-Synchronisation usw.
          
          // Dummy Beispiel: Sende Erfolgsmeldung zurück
          reply->set_success(true);
          reply->set_message("Sync erfolgreich");
          return Status::OK;
      }
  };

void RunServer() {
  std::string server_address("0.0.0.0:50051");
  SyncServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server reachable: " << server_address << std::endl;
  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();
  return 0;
}