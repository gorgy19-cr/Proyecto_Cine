#include <iostream>

#include "httplib.h"

int main() {
  httplib::Server server;

  server.Options("/api/salas", [](const httplib::Request&, httplib::Response& response) {
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    response.set_header("Access-Control-Allow-Headers", "Content-Type");
    response.status = 204;
  });

  server.Post("/api/salas", [](const httplib::Request& request, httplib::Response& response) {
    response.set_header("Access-Control-Allow-Origin", "*");

    // Los datos enviados por JavaScript llegan en request.body.
    std::cout << "Datos recibidos: " << request.body << '\n';

    response.status = 200;
    response.set_content("{\"mensaje\":\"Datos recibidos\"}", "application/json");
  });

  std::cout << "Servidor disponible en http://localhost:3000\n";
  if (!server.listen("localhost", 3000)) {
    std::cerr << "No se pudo iniciar el servidor en el puerto 3000.\n";
    return 1;
  }

  return 0;
}