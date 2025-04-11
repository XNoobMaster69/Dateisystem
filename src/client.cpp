#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <filesystem>
#include <grpcpp/grpcpp.h>

#include "./generated/dateisystem.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using dateisystem::SyncService;
using dateisystem::SyncRequest;
using dateisystem::SyncResponse;

// Dummy-Hashfunktion TODO MD5 oder SHAxx verwenden
std::string ComputeHash(const std::string& file_path) {
  try {
      std::uintmax_t filesize = std::filesystem::file_size(file_path);
      auto ftime = std::filesystem::last_write_time(file_path).time_since_epoch().count();
      return std::to_string(filesize) + "_" + std::to_string(ftime);
  } catch (std::exception& e) {
      std::cerr << "Hash-Error: " << e.what() << std::endl;
      return "";
  }
}

std::string ReadFileContent(const std::string& file_path) {
  std::ifstream in(file_path, std::ios::in | std::ios::binary);
  if (!in) {
      std::cerr << "Error while opening file: " << file_path << std::endl;
      return "";
  }
  std::ostringstream contents;
  contents << in.rdbuf();
  in.close();
  return contents.str();
}

class SyncClient {
public:
    SyncClient(std::shared_ptr<Channel> channel): stub_(SyncService::NewStub(channel)) {}

    bool SyncFile(const std::string& file_path, const std::string& hash, const std::string& file_content) {
        SyncRequest request;
        request.set_file_path(file_path);
        request.set_hash(hash);
        request.set_file_content(file_content);

        SyncResponse reply;
        ClientContext context;

        Status status = stub_->SyncFile(&context, request, &reply);
        if (status.ok()) {
            std::cout << "Server-response: " << reply.message() << std::endl;
            return reply.success();
        } else {
            std::cerr << "gRPC-error: " << status.error_message() << std::endl;
            return false;
        }
    }

private:
    std::unique_ptr<SyncService::Stub> stub_;
};

int main(int argc, char** argv) {
  if(argc < 2) {
      std::cerr << "Usage: " << argv[0] << " <Pfad zu Datei>" << std::endl;
      return 1;
  }
  std::string path = argv[1];
  SyncClient client(grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()));  // TODO Adresse anpassen

  std::string last_hash = ComputeHash(path);
  std::cout << "Waiting " << path << " for changes..." << std::endl;
  
  // Endlosschleife zur Überwachung der Datei
  while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Überprüfung alle 1000 ms
      std::string current_hash = ComputeHash(path);
      if (current_hash != last_hash) {
          std::cout << "Changes detected: " << path << std::endl;
          std::string content = ReadFileContent(path);
          client.SyncFile(path, current_hash, content);
          last_hash = current_hash;
      }
  }
  return 0;
}