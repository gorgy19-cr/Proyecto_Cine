
/*
 CINEPLUS - BACKEND EN C++

 Funciones:
 1. Servidor HTTP
 2. Conexión SQLite
 3. Autenticación del administrador
 4. Administración de salas
 5. Administración de películas
 6. Administración de funciones
 7. Matrices temporales de butacas
 8. Registro de reservaciones
*/

#include "httplib.h"
#include "json.hpp"
#include <sqlite3.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <random>
#include <stdexcept>
#include <functional>
#include <regex>
#include <cstdlib>

using json = nlohmann::json;

// ==========================================
// VARIABLES GENERALES
// ==========================================

sqlite3* db = nullptr;

std::mutex mutexDatos;

std::set<std::string> tokensAdmin;

// MATRIZ TEMPORAL DE CADA RESERVACIÓN

struct Seleccion {
    int funcionId;
    int cantidad;

    // 0 = Disponible
    // 1 = Reservada anteriormente
    // 2 = Seleccionada por este cliente
    // 3 = Inhabilitada
    std::vector<std::vector<int>> matriz;
};

std::map<std::string, Seleccion> sesiones;

// ==========================================
// 1. UTILIDADES DE SQLITE
// ==========================================

void ejecutar(const std::string& comando) {
    char* error = nullptr;

    if (sqlite3_exec(
        db, comando.c_str(),
        nullptr, nullptr, &error
    ) != SQLITE_OK) {

        std::string mensaje =
            error ? error : sqlite3_errmsg(db);

        sqlite3_free(error);

        throw std::runtime_error(mensaje);
    }
}

// CLASE PARA CONSULTAS PREPARADAS

struct Consulta {
    sqlite3_stmt* stmt = nullptr;

    Consulta(const std::string& sql) {
        if (sqlite3_prepare_v2(
            db, sql.c_str(), -1, &stmt, nullptr
        ) != SQLITE_OK) {
            throw std::runtime_error(
                sqlite3_errmsg(db)
            );
        }
    }

    ~Consulta() {
        sqlite3_finalize(stmt);
    }

    void poner(int indice, int valor) {
        sqlite3_bind_int(stmt, indice, valor);
    }

    void poner(int indice, double valor) {
        sqlite3_bind_double(stmt, indice, valor);
    }

    void poner(int indice, const std::string& valor) {
        sqlite3_bind_text(
            stmt, indice,
            valor.c_str(), -1,
            SQLITE_TRANSIENT
        );
    }

    bool siguiente() {
        int estado = sqlite3_step(stmt);

        if (estado == SQLITE_ROW) return true;
        if (estado == SQLITE_DONE) return false;

        throw std::runtime_error(
            sqlite3_errmsg(db)
        );
    }

    void guardar() {
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            throw std::runtime_error(
                sqlite3_errmsg(db)
            );
        }
    }

    int numero(int columna) {
        return sqlite3_column_int(stmt, columna);
    }

    std::string texto(int columna) {
        const unsigned char* valor =
            sqlite3_column_text(stmt, columna);

        return valor
            ? reinterpret_cast<const char*>(valor)
            : "";
    }
};

// CONVERTIR CONSULTAS A JSON

json filasJSON(Consulta& q) {
    json resultado = json::array();

    while (q.siguiente()) {
        json fila = json::object();

        int columnas =
            sqlite3_column_count(q.stmt);

        for (int i = 0; i < columnas; i++) {
            std::string nombre =
                sqlite3_column_name(q.stmt, i);

            int tipo =
                sqlite3_column_type(q.stmt, i);

            if (tipo == SQLITE_INTEGER) {
                fila[nombre] = q.numero(i);
            }
            else if (tipo == SQLITE_FLOAT) {
                fila[nombre] =
                    sqlite3_column_double(q.stmt, i);
            }
            else if (tipo == SQLITE_NULL) {
                fila[nombre] = nullptr;
            }
            else {
                fila[nombre] = q.texto(i);
            }
        }

        resultado.push_back(fila);
    }

    return resultado;
}

// ==========================================
// 2. VALIDACIONES
// ==========================================

int numeroPositivo(
    const json& j,
    const char* campo,
    int maximo = 1000000
) {
    int valor = j.at(campo).get<int>();

    if (valor < 1 || valor > maximo) {
        throw std::runtime_error(
            std::string("Valor inválido: ") + campo
        );
    }

    return valor;
}

std::string textoObligatorio(
    const json& j,
    const char* campo,
    int maximo = 150
) {
    std::string valor =
        j.at(campo).get<std::string>();

    if (
        valor.find_first_not_of(" \t\r\n")
          == std::string::npos ||
        valor.size() > static_cast<size_t>(maximo)
    ) {
        throw std::runtime_error(
            std::string("Dato inválido: ") + campo
        );
    }

    return valor;
}

bool formatoFecha(const std::string& s) {
    static const std::regex patron(
        R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}$)"
    );

    return std::regex_match(s, patron);
}

bool formatoHora(const std::string& s) {
    static const std::regex patron(
        R"(^([01][0-9]|2[0-3]):[0-5][0-9]$)"
    );

    return std::regex_match(s, patron);
}

std::string generarToken() {
    static const char hex[] =
        "0123456789abcdef";

    std::random_device rd;
    std::string token;

    for (int i = 0; i < 48; i++) {
        token += hex[rd() % 16];
    }

    return token;
}

void exigirAdmin(const httplib::Request& req) {
    auto token = req.get_header_value(
        "X-Admin-Token"
    );

    if (
        token.empty() ||
        tokensAdmin.count(token) == 0
    ) {
        throw std::runtime_error(
            "Acceso administrativo no autorizado"
        );
    }
}

int parametro(
    const httplib::Request& req,
    const char* campo
) {
    if (!req.has_param(campo)) {
        throw std::runtime_error(
            "Falta un parámetro"
        );
    }

    return std::stoi(
        req.get_param_value(campo)
    );
}

// ==========================================
// 3. CREAR BASE DE DATOS
// ==========================================

void crearTablas() {
    ejecutar("PRAGMA foreign_keys = ON;");
    ejecutar("PRAGMA busy_timeout = 5000;");

    ejecutar(R"SQL(

    CREATE TABLE IF NOT EXISTS salas (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        nombre TEXT NOT NULL UNIQUE,
        filas INTEGER NOT NULL
            CHECK(filas BETWEEN 1 AND 26),
        columnas INTEGER NOT NULL
            CHECK(columnas BETWEEN 1 AND 30),
        activa INTEGER NOT NULL DEFAULT 1
    );

    CREATE TABLE IF NOT EXISTS butacas_danadas (
        sala_id INTEGER NOT NULL,
        fila INTEGER NOT NULL,
        columna INTEGER NOT NULL,

        PRIMARY KEY(sala_id, fila, columna),

        FOREIGN KEY(sala_id)
            REFERENCES salas(id)
    );

    CREATE TABLE IF NOT EXISTS peliculas (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        nombre TEXT NOT NULL,
        inicio TEXT NOT NULL,
        fin TEXT NOT NULL,
        precio INTEGER NOT NULL CHECK(precio > 0),
        sala_id INTEGER NOT NULL,

        FOREIGN KEY(sala_id)
            REFERENCES salas(id)
    );

    CREATE TABLE IF NOT EXISTS funciones (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        pelicula_id INTEGER NOT NULL,
        sala_id INTEGER NOT NULL,
        fecha TEXT NOT NULL,
        hora TEXT NOT NULL,

        UNIQUE(sala_id, fecha, hora),

        FOREIGN KEY(pelicula_id)
            REFERENCES peliculas(id),

        FOREIGN KEY(sala_id)
            REFERENCES salas(id)
    );

    CREATE TABLE IF NOT EXISTS clientes (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        nombre TEXT NOT NULL,
        telefono TEXT NOT NULL
    );

    CREATE TABLE IF NOT EXISTS reservas (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        cliente_id INTEGER NOT NULL,
        funcion_id INTEGER NOT NULL,
        fila INTEGER NOT NULL,
        columna INTEGER NOT NULL,

        UNIQUE(funcion_id, fila, columna),

        FOREIGN KEY(cliente_id)
            REFERENCES clientes(id),

        FOREIGN KEY(funcion_id)
            REFERENCES funciones(id)
    );

    )SQL");
}

// ==========================================
// 4. RESPUESTAS HTTP
// ==========================================

void responder(
    httplib::Response& res,
    const json& contenido,
    int estado = 200
) {
    res.status = estado;

    res.set_content(
        contenido.dump(),
        "application/json; charset=utf-8"
    );
}

// FUNCIÓN PARA RUTAS GET

void rutaGET(
    httplib::Server& servidor,
    const std::string& ruta,
    std::function<json(const httplib::Request&)> accion
) {
    servidor.Get(
        ruta,
        [accion](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            try {
                std::lock_guard<std::mutex> lock(
                    mutexDatos
                );

                responder(res, accion(req));

            } catch (const std::exception& e) {
                responder(
                    res,
                    {{"error", e.what()}},
                    400
                );
            }
        }
    );
}

// FUNCIÓN PARA RUTAS POST

void rutaPOST(
    httplib::Server& servidor,
    const std::string& ruta,
    std::function<
        json(const httplib::Request&, const json&)
    > accion
) {
    servidor.Post(
        ruta,
        [accion](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            try {
                json datos = json::parse(req.body);

                std::lock_guard<std::mutex> lock(
                    mutexDatos
                );

                responder(res, accion(req, datos));

            } catch (const std::exception& e) {
                responder(
                    res,
                    {{"error", e.what()}},
                    400
                );
            }
        }
    );
}

// ==========================================
// 5. COMPROBAR FUNCIONES DISPONIBLES
// ==========================================

void verificarFuncion(int id) {
    Consulta q(R"SQL(
        SELECT 1
        FROM funciones f
        JOIN salas s ON s.id = f.sala_id
        JOIN peliculas p ON p.id = f.pelicula_id
        WHERE f.id = ?
          AND s.activa = 1
          AND date('now','localtime')
              BETWEEN p.inicio AND p.fin
          AND (
              f.fecha > date('now','localtime')
              OR (
                  f.fecha = date('now','localtime')
                  AND f.hora >= time('now','localtime')
              )
          )
    )SQL");

    q.poner(1, id);

    if (!q.siguiente()) {
        throw std::runtime_error(
            "La función ya no está disponible"
        );
    }
}

// ==========================================
// 6. CREAR MATRIZ TEMPORAL EN C++
// ==========================================

Seleccion crearMatriz(int funcionId, int cantidad) {
    verificarFuncion(funcionId);

    Consulta sala(R"SQL(
        SELECT s.id, s.filas, s.columnas
        FROM funciones f
        JOIN salas s ON s.id = f.sala_id
        WHERE f.id = ?
    )SQL");

    sala.poner(1, funcionId);

    if (!sala.siguiente()) {
        throw std::runtime_error(
            "La función no existe"
        );
    }

    int salaId = sala.numero(0);
    int filas = sala.numero(1);
    int columnas = sala.numero(2);

    Seleccion seleccion;
    seleccion.funcionId = funcionId;
    seleccion.cantidad = cantidad;

    // Inicialmente todas las posiciones son libres.
    seleccion.matriz.assign(
        filas,
        std::vector<int>(columnas, 0)
    );

    // Cargar las butacas confirmadas anteriormente.
    Consulta ocupadas(
        "SELECT fila,columna FROM reservas "
        "WHERE funcion_id=?"
    );

    ocupadas.poner(1, funcionId);

    while (ocupadas.siguiente()) {
        int f = ocupadas.numero(0);
        int c = ocupadas.numero(1);

        if (
            f >= 0 && f < filas &&
            c >= 0 && c < columnas
        ) {
            seleccion.matriz[f][c] = 1;
        }
    }

    // Cargar las butacas inhabilitadas.
    Consulta danadas(
        "SELECT fila,columna FROM butacas_danadas "
        "WHERE sala_id=?"
    );

    danadas.poner(1, salaId);

    while (danadas.siguiente()) {
        int f = danadas.numero(0);
        int c = danadas.numero(1);

        if (
            f >= 0 && f < filas &&
            c >= 0 && c < columnas &&
            seleccion.matriz[f][c] != 1
        ) {
            seleccion.matriz[f][c] = 3;
        }
    }

    int disponibles = 0;

    for (const auto& fila : seleccion.matriz) {
        for (int estado : fila) {
            if (estado == 0) disponibles++;
        }
    }

    if (disponibles < cantidad) {
        throw std::runtime_error(
            "No hay suficientes butacas disponibles"
        );
    }

    return seleccion;
}

// OBTENER SOLO LAS BUTACAS NUEVAS (ESTADO 2)

json nuevasButacas(const Seleccion& s) {
    json nuevas = json::array();

    for (
        int f = 0;
        f < static_cast<int>(s.matriz.size());
        f++
    ) {
        for (
            int c = 0;
            c < static_cast<int>(s.matriz[f].size());
            c++
        ) {
            if (s.matriz[f][c] == 2) {
                nuevas.push_back({
                    {"fila", f},
                    {"columna", c}
                });
            }
        }
    }

    return nuevas;
}

int contarNuevas(const Seleccion& s) {
    return static_cast<int>(
        nuevasButacas(s).size()
    );
}

// ==========================================
// 7. PROGRAMA PRINCIPAL
// ==========================================

int main() {

    // CONTRASEÑA DESDE VARIABLE DE ENTORNO
    const char* variableClave =
        std::getenv("CINE_ADMIN_PASSWORD");

    if (
        variableClave == nullptr ||
        std::string(variableClave).size() < 8
    ) {
        std::cerr
            << "ERROR: Configura CINE_ADMIN_PASSWORD "
            << "con al menos 8 caracteres.\n";

        return 1;
    }

    std::string claveAdmin = variableClave;

    // ABRIR O CREAR cine.db

    if (
        sqlite3_open("cine.db", &db) != SQLITE_OK
    ) {
        std::cerr
            << "No se pudo abrir cine.db\n";

        return 1;
    }

    try {
        crearTablas();

    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        sqlite3_close(db);
        return 1;
    }

    httplib::Server servidor;

    // ======================================
    // SERVIR HTML, CSS Y JAVASCRIPT
    // ======================================

    auto archivo = [](
        const std::string& nombre,
        const std::string& tipo,
        httplib::Response& res
    ) {
        std::ifstream f(nombre, std::ios::binary);

        if (!f) {
            res.status = 404;
            res.set_content(
                "Archivo no encontrado",
                "text/plain"
            );
            return;
        }

        std::ostringstream contenido;
        contenido << f.rdbuf();

        res.set_content(contenido.str(), tipo);
    };

    servidor.Get(
        "/",
        [archivo](const httplib::Request&,
                  httplib::Response& res) {
            archivo(
                "index.html",
                "text/html; charset=utf-8",
                res
            );
        }
    );

    servidor.Get(
        "/styles.css",
        [archivo](const httplib::Request&,
                  httplib::Response& res) {
            archivo(
                "styles.css",
                "text/css; charset=utf-8",
                res
            );
        }
    );

    servidor.Get(
        "/app.js",
        [archivo](const httplib::Request&,
                  httplib::Response& res) {
            archivo(
                "app.js",
                "application/javascript; charset=utf-8",
                res
            );
        }
    );

    // ======================================
    // 8. INICIAR SESIÓN ADMINISTRATIVA
    // ======================================

    rutaPOST(
        servidor, "/api/admin/login",
        [&](const httplib::Request&, const json& j) {

            if (
                j.at("clave").get<std::string>()
                != claveAdmin
            ) {
                throw std::runtime_error(
                    "Contraseña incorrecta"
                );
            }

            std::string token = generarToken();
            tokensAdmin.insert(token);

            return json{{"token", token}};
        }
    );

    // ======================================
    // 9. CONSULTAR SALAS
    // ======================================

    rutaGET(
        servidor, "/api/salas",
        [](const httplib::Request&) {

            Consulta q(
                "SELECT id,nombre,filas,columnas,activa "
                "FROM salas ORDER BY id DESC"
            );

            return filasJSON(q);
        }
    );

    // CREAR SALAS: SOLO ADMINISTRADOR

    rutaPOST(
        servidor, "/api/salas",
        [](const httplib::Request& req, const json& j) {

            exigirAdmin(req);

            Consulta q(
                "INSERT INTO salas(nombre,filas,columnas) "
                "VALUES(?,?,?)"
            );

            q.poner(
                1, textoObligatorio(j, "nombre", 50)
            );

            q.poner(
                2, numeroPositivo(j, "filas", 26)
            );

            q.poner(
                3, numeroPositivo(j, "columnas", 30)
            );

            q.guardar();

            return json{
                {"mensaje", "Sala registrada"}
            };
        }
    );

    // HABILITAR O INHABILITAR SALAS

    rutaPOST(
        servidor, "/api/salas/estado",
        [](const httplib::Request& req, const json& j) {

            exigirAdmin(req);

            Consulta q(
                "UPDATE salas SET activa=? WHERE id=?"
            );

            q.poner(
                1, j.at("activa").get<bool>() ? 1 : 0
            );

            q.poner(
                2, numeroPositivo(j, "id")
            );

            q.guardar();

            return json{
                {"mensaje", "Sala actualizada"}
            };
        }
    );

    // ======================================
    // 10. CONSULTAR BUTACAS INHABILITADAS
    // ======================================

    rutaGET(
        servidor, "/api/butacas",
        [](const httplib::Request& req) {

            Consulta q(
                "SELECT fila,columna "
                "FROM butacas_danadas "
                "WHERE sala_id=?"
            );

            q.poner(
                1, parametro(req, "sala_id")
            );

            return filasJSON(q);
        }
    );

    // CAMBIAR ESTADO DE UNA BUTACA

    rutaPOST(
        servidor, "/api/butacas/estado",
        [](const httplib::Request& req, const json& j) {

            exigirAdmin(req);

            int salaId =
                numeroPositivo(j, "sala_id");

            int fila = j.at("fila").get<int>();
            int columna = j.at("columna").get<int>();

            Consulta sala(
                "SELECT filas,columnas "
                "FROM salas WHERE id=?"
            );

            sala.poner(1, salaId);

            if (!sala.siguiente()) {
                throw std::runtime_error(
                    "La sala no existe"
                );
            }

            if (
                fila < 0 || columna < 0 ||
                fila >= sala.numero(0) ||
                columna >= sala.numero(1)
            ) {
                throw std::runtime_error(
                    "Butaca fuera de rango"
                );
            }

            bool inhabilitar =
                j.at("inhabilitada").get<bool>();

            if (inhabilitar) {
                Consulta q(
                    "INSERT OR IGNORE INTO "
                    "butacas_danadas "
                    "(sala_id,fila,columna) "
                    "VALUES(?,?,?)"
                );

                q.poner(1, salaId);
                q.poner(2, fila);
                q.poner(3, columna);
                q.guardar();

            } else {
                Consulta q(
                    "DELETE FROM butacas_danadas "
                    "WHERE sala_id=? "
                    "AND fila=? AND columna=?"
                );

                q.poner(1, salaId);
                q.poner(2, fila);
                q.poner(3, columna);
                q.guardar();
            }

            return json{
                {"mensaje", "Butaca actualizada"}
            };
        }
    );

    // ======================================
    // 11. CONSULTAR PELÍCULAS
    // ======================================

    rutaGET(
        servidor, "/api/peliculas",
        [](const httplib::Request&) {

            Consulta q(R"SQL(
                SELECT p.id,p.nombre,p.inicio,p.fin,
                       p.precio,p.sala_id,
                       s.nombre AS sala
                FROM peliculas p
                JOIN salas s ON s.id=p.sala_id
                ORDER BY p.id DESC
            )SQL");

            return filasJSON(q);
        }
    );

    // REGISTRAR NUEVA PELÍCULA

    rutaPOST(
        servidor, "/api/peliculas",
        [](const httplib::Request& req, const json& j) {

            exigirAdmin(req);

            std::string inicio =
                textoObligatorio(j, "inicio");

            std::string fin =
                textoObligatorio(j, "fin");

            if (
                !formatoFecha(inicio) ||
                !formatoFecha(fin) ||
                inicio > fin
            ) {
                throw std::runtime_error(
                    "Fechas de exhibición inválidas"
                );
            }

            int salaId =
                numeroPositivo(j, "sala_id");

            Consulta verificar(
                "SELECT 1 FROM salas "
                "WHERE id=? AND activa=1"
            );

            verificar.poner(1, salaId);

            if (!verificar.siguiente()) {
                throw std::runtime_error(
                    "Sala no disponible"
                );
            }

            Consulta q(
                "INSERT INTO peliculas "
                "(nombre,inicio,fin,precio,sala_id) "
                "VALUES(?,?,?,?,?)"
            );

            q.poner(
                1, textoObligatorio(j, "nombre")
            );

            q.poner(2, inicio);
            q.poner(3, fin);

            q.poner(
                4, numeroPositivo(j, "precio")
            );

            q.poner(5, salaId);
            q.guardar();

            return json{
                {"mensaje", "Película registrada"}
            };
        }
    );

    // ======================================
    // 12. CONSULTAR FUNCIONES
    // ======================================

    rutaGET(
        servidor, "/api/funciones",
        [](const httplib::Request&) {

            Consulta q(R"SQL(
                SELECT f.id,f.pelicula_id,f.sala_id,
                       f.fecha,f.hora,
                       p.nombre AS pelicula,
                       p.precio,
                       s.nombre AS sala
                FROM funciones f
                JOIN peliculas p
                    ON p.id=f.pelicula_id
                JOIN salas s
                    ON s.id=f.sala_id
                ORDER BY f.fecha,f.hora
            )SQL");

            return filasJSON(q);
        }
    );

    // CREAR FUNCIONES

    rutaPOST(
        servidor, "/api/funciones",
        [](const httplib::Request& req, const json& j) {

            exigirAdmin(req);

            int pelicula =
                numeroPositivo(j, "pelicula_id");

            int sala =
                numeroPositivo(j, "sala_id");

            std::string fecha =
                textoObligatorio(j, "fecha");

            std::string hora =
                textoObligatorio(j, "hora");

            if (
                !formatoFecha(fecha) ||
                !formatoHora(hora)
            ) {
                throw std::runtime_error(
                    "Fecha u hora inválida"
                );
            }

            Consulta check(R"SQL(
                SELECT 1
                FROM peliculas p
                JOIN salas s ON s.id=p.sala_id
                WHERE p.id=?
                  AND p.sala_id=?
                  AND s.activa=1
                  AND ? BETWEEN p.inicio AND p.fin
            )SQL");

            check.poner(1, pelicula);
            check.poner(2, sala);
            check.poner(3, fecha);

            if (!check.siguiente()) {
                throw std::runtime_error(
                    "Película, fecha o sala no válida"
                );
            }

            Consulta q(
                "INSERT INTO funciones "
                "(pelicula_id,sala_id,fecha,hora) "
                "VALUES(?,?,?,?)"
            );

            q.poner(1, pelicula);
            q.poner(2, sala);
            q.poner(3, fecha);
            q.poner(4, hora);
            q.guardar();

            return json{
                {"mensaje", "Función registrada"}
            };
        }
    );

    // ======================================
    // 13. CARTELERA VIGENTE
    // ======================================

    rutaGET(
        servidor, "/api/cartelera",
        [](const httplib::Request&) {

            Consulta q(R"SQL(
                SELECT DISTINCT
                    p.id,p.nombre,p.precio
                FROM peliculas p
                JOIN funciones f
                    ON f.pelicula_id=p.id
                JOIN salas s
                    ON s.id=f.sala_id
                WHERE s.activa=1
                  AND date('now','localtime')
                      BETWEEN p.inicio AND p.fin
                  AND (
                      f.fecha > date('now','localtime')
                      OR (
                          f.fecha=date('now','localtime')
                          AND f.hora >= time('now','localtime')
                      )
                  )
                ORDER BY p.nombre
            )SQL");

            return filasJSON(q);
        }
    );

    // ======================================
    // 14. INICIAR UNA SELECCIÓN TEMPORAL
    // ======================================

    rutaPOST(
        servidor, "/api/seleccion/iniciar",
        [](const httplib::Request&, const json& j) {

            int funcion =
                numeroPositivo(j, "funcion_id");

            int cantidad =
                numeroPositivo(j, "cantidad", 20);

            Seleccion seleccion =
                crearMatriz(funcion, cantidad);

            std::string token = generarToken();

            sesiones[token] = seleccion;

            return json{
                {"sesion", token},
                {"matriz", seleccion.matriz}
            };
        }
    );

    // ======================================
    // 15. MARCAR / DESMARCAR UNA BUTACA
    // ======================================

    rutaPOST(
        servidor, "/api/seleccion/marcar",
        [](const httplib::Request&, const json& j) {

            std::string token =
                textoObligatorio(j, "sesion");

            auto it = sesiones.find(token);

            if (it == sesiones.end()) {
                throw std::runtime_error(
                    "Sesión de reservación inválida"
                );
            }

            Seleccion& s = it->second;

            int fila = j.at("fila").get<int>();
            int columna = j.at("columna").get<int>();

            if (
                fila < 0 || columna < 0 ||
                fila >= static_cast<int>(s.matriz.size()) ||
                columna >= static_cast<int>(
                    s.matriz[fila].size()
                )
            ) {
                throw std::runtime_error(
                    "Butaca fuera de rango"
                );
            }

            int& estado = s.matriz[fila][columna];

            if (estado == 1 || estado == 3) {
                throw std::runtime_error(
                    "Butaca no disponible"
                );
            }

            if (estado == 2) {
                // Desmarcar una selección actual.
                estado = 0;
            }
            else {
                if (contarNuevas(s) >= s.cantidad) {
                    throw std::runtime_error(
                        "Ya seleccionaste la cantidad solicitada"
                    );
                }

                // Marcar como nueva reservación.
                estado = 2;
            }

            return json{
                {"matriz", s.matriz},
                {"seleccionadas", nuevasButacas(s)}
            };
        }
    );

    // ======================================
    // 16. CONFIRMAR RESERVACIÓN EN SQLITE
    // ======================================

    rutaPOST(
        servidor, "/api/reservas",
        [](const httplib::Request&, const json& j) {

            std::string token =
                textoObligatorio(j, "sesion");

            auto it = sesiones.find(token);

            if (it == sesiones.end()) {
                throw std::runtime_error(
                    "La selección ya no está disponible"
                );
            }

            Seleccion& s = it->second;

            std::string nombre =
                textoObligatorio(j, "nombre", 100);

            std::string telefono =
                textoObligatorio(j, "telefono", 20);

            json nuevas = nuevasButacas(s);

            if (
                static_cast<int>(nuevas.size())
                != s.cantidad
            ) {
                throw std::runtime_error(
                    "Cantidad de butacas incorrecta"
                );
            }

            // TRANSACCIÓN: TODO SE GUARDA O NADA.

            ejecutar("BEGIN IMMEDIATE TRANSACTION;");

            try {
                // Comprobar otra vez que la función
                // todavía esté disponible.

                verificarFuncion(s.funcionId);

                Consulta sala(R"SQL(
                    SELECT sala_id
                    FROM funciones
                    WHERE id=?
                )SQL");

                sala.poner(1, s.funcionId);

                if (!sala.siguiente()) {
                    throw std::runtime_error(
                        "Función inexistente"
                    );
                }

                int salaId = sala.numero(0);

                // REVISAR SOLAMENTE ESTADOS NUEVOS.

                for (const auto& b : nuevas) {
                    int fila = b.at("fila").get<int>();
                    int columna = b.at("columna").get<int>();

                    // Comprobar reservas anteriores.
                    Consulta ocupada(
                        "SELECT 1 FROM reservas "
                        "WHERE funcion_id=? "
                        "AND fila=? AND columna=?"
                    );

                    ocupada.poner(1, s.funcionId);
                    ocupada.poner(2, fila);
                    ocupada.poner(3, columna);

                    if (ocupada.siguiente()) {
                        throw std::runtime_error(
                            "Una butaca fue reservada "
                            "por otro cliente"
                        );
                    }

                    // Comprobar butacas dañadas.
                    Consulta danada(
                        "SELECT 1 FROM butacas_danadas "
                        "WHERE sala_id=? "
                        "AND fila=? AND columna=?"
                    );

                    danada.poner(1, salaId);
                    danada.poner(2, fila);
                    danada.poner(3, columna);

                    if (danada.siguiente()) {
                        throw std::runtime_error(
                            "Una butaca está inhabilitada"
                        );
                    }
                }

                // GUARDAR CLIENTE.

                Consulta cliente(
                    "INSERT INTO clientes "
                    "(nombre,telefono) VALUES(?,?)"
                );

                cliente.poner(1, nombre);
                cliente.poner(2, telefono);
                cliente.guardar();

                int clienteId =
                    static_cast<int>(
                        sqlite3_last_insert_rowid(db)
                    );

                int primeraReserva = 0;

                // GUARDAR SOLO LAS BUTACAS NUEVAS.

                for (const auto& b : nuevas) {
                    Consulta guardar(
                        "INSERT INTO reservas "
                        "(cliente_id,funcion_id,fila,columna) "
                        "VALUES(?,?,?,?)"
                    );

                    guardar.poner(1, clienteId);
                    guardar.poner(2, s.funcionId);

                    guardar.poner(
                        3, b.at("fila").get<int>()
                    );

                    guardar.poner(
                        4, b.at("columna").get<int>()
                    );

                    guardar.guardar();

                    int id = static_cast<int>(
                        sqlite3_last_insert_rowid(db)
                    );

                    if (primeraReserva == 0) {
                        primeraReserva = id;
                    }
                }

                ejecutar("COMMIT;");

                // Ya no se puede modificar esta selección.
                sesiones.erase(it);

                return json{
                    {"mensaje", "Reservación confirmada"},
                    {"reserva_id", primeraReserva},
                    {"cantidad", nuevas.size()}
                };

            } catch (...) {
                ejecutar("ROLLBACK;");

                // Invalidar la selección conflictiva.
                sesiones.erase(token);

                throw;
            }
        }
    );

    // ======================================
    // 17. INICIAR EL SERVIDOR
    // ======================================

    std::cout
        << "================================\n"
        << "CINEPLUS INICIADO\n"
        << "http://localhost:3000\n"
        << "================================\n";

    bool funcionando =
        servidor.listen("127.0.0.1", 3000);

    sqlite3_close(db);

    return funcionando ? 0 : 1;
}
