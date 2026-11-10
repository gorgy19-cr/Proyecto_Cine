
/* ==========================================
   CINEPLUS - JAVASCRIPT
   Formularios, navegación y conexión con C++
   ========================================== */

const $ = id => document.getElementById(id);

// DATOS QUE MOSTRAREMOS EN LA PÁGINA

let rol = "";
let adminToken = "";

let salas = [];
let peliculas = [];
let funciones = [];
let cartelera = [];

let reserva = {
  nombre: "",
  telefono: "",
  pelicula: null,
  funcion: null,
  cantidad: 0,
  sesion: "",
  matriz: [],
  seleccionadas: []
};

// ------------------------------------------
// 1. COMUNICACIÓN CON EL BACKEND
// ------------------------------------------

async function api(ruta, datos) {
  const opciones = {
    headers: {
      "Content-Type": "application/json"
    }
  };

  // El token permite autorizar operaciones administrativas.
  if (adminToken) {
    opciones.headers["X-Admin-Token"] = adminToken;
  }

  if (datos !== undefined) {
    opciones.method = "POST";
    opciones.body = JSON.stringify(datos);
  }

  const respuesta = await fetch("/api/" + ruta, opciones);

  const json = await respuesta.json();

  if (!respuesta.ok) {
    throw new Error(json.error || "Error del servidor");
  }

  return json;
}

// ------------------------------------------
// 2. FUNCIONES AUXILIARES
// ------------------------------------------

function informar(id, texto) {
  $(id).textContent = texto;
}

function mostrarPaso(paso) {
  const pasos = [
    "paso-cliente",
    "paso-funcion",
    "paso-cantidad",
    "paso-butacas",
    "paso-confirmacion",
    "reserva-exitosa"
  ];

  pasos.forEach(id => {
    $(id).hidden = id !== paso;
  });
}

function crearBoton(texto, accion) {
  const b = document.createElement("button");
  b.type = "button";
  b.textContent = texto;
  b.addEventListener("click", accion);
  return b;
}

function crearCelda(texto) {
  const td = document.createElement("td");
  td.textContent = texto;
  return td;
}

function llenarSelect(id, datos, texto, etiqueta) {
  const select = $(id);
  select.replaceChildren();

  select.add(new Option(texto, ""));

  datos.forEach(d => {
    select.add(new Option(etiqueta(d), String(d.id)));
  });
}

function dinero(valor) {
  return "₡" + Number(valor).toLocaleString("es-CR");
}

function fechaLocal() {
  const d = new Date();
  const z = n => String(n).padStart(2, "0");

  return `${d.getFullYear()}-${z(d.getMonth() + 1)}-${z(d.getDate())}`;
}

function fechaHoraLocal() {
  const d = new Date();
  const z = n => String(n).padStart(2, "0");

  return fechaLocal() + "T" +
    z(d.getHours()) + ":" + z(d.getMinutes());
}

function nombreButaca(fila, columna) {
  return String.fromCharCode(65 + fila) + (columna + 1);
}

function controlarError(error, destino) {
  console.error(error);

  if (destino) {
    informar(destino, error.message);
  } else {
    alert(error.message);
  }
}

// ------------------------------------------
// 3. INGRESAR COMO CLIENTE
// ------------------------------------------

$("entrar-cliente").onclick = async () => {
  try {
    rol = "cliente";
    adminToken = "";

    $("acceso").hidden = true;
    $("aplicacion").hidden = false;
    $("inicio").hidden = false;
    $("reservar").hidden = false;
    $("administracion").hidden = true;

    $("nav-inicio").hidden = false;
    $("nav-reservar").hidden = false;
    $("nav-administracion").hidden = true;

    await cargarDatos();

    mostrarPaso("paso-cliente");
    location.hash = "inicio";

  } catch (error) {
    controlarError(error);
  }
};

// ------------------------------------------
// 4. INGRESAR COMO ADMINISTRADOR
// ------------------------------------------

$("entrar-admin").onclick = () => {
  $("acceso").hidden = true;
  $("login-admin").hidden = false;
};

$("cancelar-login").onclick = () => {
  $("form-login").reset();
  $("login-admin").hidden = true;
  $("acceso").hidden = false;
};

$("form-login").addEventListener("submit", async e => {
  e.preventDefault();

  try {
    const datos = await api("admin/login", {
      clave: $("admin-clave").value
    });

    adminToken = datos.token;
    rol = "administrador";

    $("form-login").reset();
    $("login-admin").hidden = true;

    $("aplicacion").hidden = false;
    $("inicio").hidden = true;
    $("reservar").hidden = true;
    $("administracion").hidden = false;

    $("nav-inicio").hidden = true;
    $("nav-reservar").hidden = true;
    $("nav-administracion").hidden = false;

    await cargarDatos();
    location.hash = "administracion";

  } catch (error) {
    controlarError(error, "mensaje-login");
  }
});

// SALIR Y CAMBIAR DE USUARIO

$("salir").onclick = () => {
  rol = "";
  adminToken = "";

  reserva.sesion = "";
  reserva.matriz = [];

  $("aplicacion").hidden = true;
  $("login-admin").hidden = true;
  $("acceso").hidden = false;

  history.replaceState(null, "", location.pathname);
  window.scrollTo(0, 0);
};

// ------------------------------------------
// 5. CONSULTAR INFORMACIÓN DE SQLITE
// ------------------------------------------

async function cargarDatos() {
  [salas, peliculas, funciones, cartelera] =
    await Promise.all([
      api("salas"),
      api("peliculas"),
      api("funciones"),
      api("cartelera")
    ]);

  llenarSelect(
    "pelicula-sala",
    salas.filter(s => s.activa),
    "Seleccioná una sala",
    s => s.nombre
  );

  llenarSelect(
    "funcion-sala",
    salas.filter(s => s.activa),
    "Seleccioná una sala",
    s => s.nombre
  );

  llenarSelect(
    "admin-sala-butacas",
    salas,
    "Seleccioná una sala",
    s => s.nombre
  );

  llenarSelect(
    "funcion-pelicula",
    peliculas,
    "Seleccioná una película",
    p => p.nombre
  );

  llenarSelect(
    "reserva-pelicula",
    cartelera,
    "Seleccioná una película",
    p => p.nombre
  );

  dibujarSalas();
  dibujarPeliculas();
  dibujarFunciones();
  dibujarCartelera();
}

// ------------------------------------------
// 6. REGISTRAR SALAS
// ------------------------------------------

$("form-sala").addEventListener("submit", async e => {
  e.preventDefault();

  try {
    await api("salas", {
      nombre: $("sala-nombre").value.trim(),
      filas: Number($("sala-filas").value),
      columnas: Number($("sala-columnas").value)
    });

    e.target.reset();

    await cargarDatos();

    informar("mensaje-salas", "Sala registrada correctamente.");

  } catch (error) {
    controlarError(error, "mensaje-salas");
  }
});

// MOSTRAR SALAS REGISTRADAS

function dibujarSalas() {
  const tabla = $("tabla-salas");
  tabla.replaceChildren();

  salas.forEach(s => {
    const tr = document.createElement("tr");

    [
      s.nombre,
      s.filas,
      s.columnas,
      s.activa ? "Activa" : "Inhabilitada"
    ].forEach(v => tr.append(crearCelda(v)));

    const acciones = crearCelda("");

    const b = crearBoton(
      s.activa ? "Inhabilitar" : "Habilitar",
      async () => {
        try {
          await api("salas/estado", {
            id: s.id,
            activa: !s.activa
          });

          await cargarDatos();

        } catch (error) {
          controlarError(error);
        }
      }
    );

    acciones.append(b);
    tr.append(acciones);
    tabla.append(tr);
  });
}

// ------------------------------------------
// 7. INHABILITAR BUTACAS
// ------------------------------------------

$("admin-sala-butacas").onchange =
  mostrarButacasAdministrador;

async function mostrarButacasAdministrador() {
  const contenedor = $("mapa-butacas-admin");
  contenedor.replaceChildren();

  const sala = salas.find(
    s => s.id === Number($("admin-sala-butacas").value)
  );

  if (!sala) return;

  try {
    const danadas = await api("butacas?sala_id=" + sala.id);

    const bloqueadas = new Set(
      danadas.map(b => `${b.fila}-${b.columna}`)
    );

    for (let f = 0; f < sala.filas; f++) {
      const fila = document.createElement("div");
      fila.className = "fila-butacas";

      for (let c = 0; c < sala.columnas; c++) {
        const bloqueada = bloqueadas.has(`${f}-${c}`);

        const b = crearBoton(
          nombreButaca(f, c),
          async () => {
            try {
              await api("butacas/estado", {
                sala_id: sala.id,
                fila: f,
                columna: c,
                inhabilitada: !bloqueada
              });

              await mostrarButacasAdministrador();

              informar(
                "mensaje-admin-butacas",
                "Estado de la butaca actualizado."
              );

            } catch (error) {
              controlarError(error, "mensaje-admin-butacas");
            }
          }
        );

        b.className = "butaca " +
          (bloqueada ? "inhabilitada" : "disponible");

        fila.append(b);
      }

      contenedor.append(fila);
    }

  } catch (error) {
    controlarError(error, "mensaje-admin-butacas");
  }
}

// ------------------------------------------
// 8. REGISTRAR PELÍCULAS
// ------------------------------------------

$("form-pelicula").addEventListener("submit", async e => {
  e.preventDefault();

  try {
    await api("peliculas", {
      nombre: $("pelicula-nombre").value.trim(),
      inicio: $("pelicula-inicio").value,
      fin: $("pelicula-fin").value,
      precio: Number($("pelicula-precio").value),
      sala_id: Number($("pelicula-sala").value)
    });

    e.target.reset();

    await cargarDatos();

    informar(
      "mensaje-peliculas",
      "Película registrada correctamente."
    );

  } catch (error) {
    controlarError(error, "mensaje-peliculas");
  }
});

function dibujarPeliculas() {
  const tabla = $("tabla-peliculas");
  tabla.replaceChildren();

  peliculas.forEach(p => {
    const tr = document.createElement("tr");

    [
      p.nombre,
      p.inicio + " a " + p.fin,
      dinero(p.precio),
      p.sala
    ].forEach(v => tr.append(crearCelda(v)));

    tabla.append(tr);
  });
}

// ------------------------------------------
// 9. REGISTRAR FUNCIONES
// ------------------------------------------

// Seleccionar automáticamente la sala de la película.

$("funcion-pelicula").onchange = () => {
  const p = peliculas.find(
    x => x.id === Number($("funcion-pelicula").value)
  );

  $("funcion-sala").value =
    p ? String(p.sala_id) : "";
};

$("form-funcion-admin").addEventListener(
  "submit",
  async e => {
    e.preventDefault();

    try {
      await api("funciones", {
        pelicula_id: Number($("funcion-pelicula").value),
        sala_id: Number($("funcion-sala").value),
        fecha: $("funcion-fecha").value,
        hora: $("funcion-hora").value
      });

      e.target.reset();
      await cargarDatos();

      informar(
        "mensaje-funciones",
        "Función registrada correctamente."
      );

    } catch (error) {
      controlarError(error, "mensaje-funciones");
    }
  }
);

function dibujarFunciones() {
  const tabla = $("tabla-funciones");
  tabla.replaceChildren();

  funciones.forEach(f => {
    const tr = document.createElement("tr");

    [
      f.pelicula,
      f.sala,
      f.fecha,
      f.hora
    ].forEach(v => tr.append(crearCelda(v)));

    tabla.append(tr);
  });
}

// ------------------------------------------
// 10. MOSTRAR CARTELERA AL CLIENTE
// ------------------------------------------

function dibujarCartelera() {
  const contenedor = $("cartelera");
  contenedor.replaceChildren();

  informar(
    "mensaje-cartelera",
    cartelera.length
      ? ""
      : "No hay funciones disponibles por ahora."
  );

  cartelera.forEach(p => {
    const tarjeta = document.createElement("article");
    tarjeta.className = "tarjeta-pelicula";

    const poster = document.createElement("div");
    poster.className = "poster-pelicula";
    poster.textContent = "🎬";

    const contenido = document.createElement("div");
    contenido.className = "contenido-pelicula";

    const titulo = document.createElement("h3");
    titulo.textContent = p.nombre;

    const precio = document.createElement("p");
    precio.textContent = "Entrada: " + dinero(p.precio);

    const reservar = crearBoton("Reservar", () => {
      $("reserva-pelicula").value = String(p.id);
      cargarFuncionesDisponibles();

      mostrarPaso("paso-cliente");
      location.hash = "reservar";
    });

    contenido.append(titulo, precio, reservar);
    tarjeta.append(poster, contenido);
    contenedor.append(tarjeta);
  });
}

// ------------------------------------------
// 11. FUNCIONES DISPONIBLES PARA EL CLIENTE
// ------------------------------------------

function cargarFuncionesDisponibles() {
  const peliculaId = Number($("reserva-pelicula").value);

  const disponibles = funciones.filter(f =>
    f.pelicula_id === peliculaId &&
    `${f.fecha}T${f.hora}` >= fechaHoraLocal() &&
    salas.some(s => s.id === f.sala_id && s.activa)
  );

  llenarSelect(
    "reserva-funcion",
    disponibles,
    "Seleccioná fecha y hora",
    f => `${f.fecha} - ${f.hora} - ${f.sala}`
  );

  $("reserva-funcion").disabled =
    disponibles.length === 0;
}

$("reserva-pelicula").onchange =
  cargarFuncionesDisponibles;

// ------------------------------------------
// 12. PASO 1 - DATOS DEL CLIENTE
// ------------------------------------------

$("form-cliente").addEventListener("submit", e => {
  e.preventDefault();

  reserva.nombre = $("cliente-nombre").value.trim();
  reserva.telefono = $("cliente-telefono").value.trim();

  if (!reserva.nombre || !reserva.telefono) {
    alert("Completá tus datos.");
    return;
  }

  cargarFuncionesDisponibles();
  mostrarPaso("paso-funcion");
});

// ------------------------------------------
// 13. PASO 2 - SELECCIÓN DE FUNCIÓN
// ------------------------------------------

$("form-funcion").addEventListener("submit", e => {
  e.preventDefault();

  reserva.pelicula = cartelera.find(
    p => p.id === Number($("reserva-pelicula").value)
  );

  reserva.funcion = funciones.find(
    f => f.id === Number($("reserva-funcion").value)
  );

  if (!reserva.pelicula || !reserva.funcion) {
    alert("Seleccioná una función válida.");
    return;
  }

  $("precio-unitario").textContent =
    dinero(reserva.pelicula.precio);

  actualizarPrecio();
  mostrarPaso("paso-cantidad");
});

// ------------------------------------------
// 14. PASO 3 - CANTIDAD DE ENTRADAS
// ------------------------------------------

function actualizarPrecio() {
  const cantidad = Number($("cantidad").value);
  const precio = reserva.pelicula?.precio || 0;

  $("total").textContent = dinero(cantidad * precio);
}

$("cantidad").oninput = actualizarPrecio;

$("form-cantidad").addEventListener("submit", async e => {
  e.preventDefault();

  reserva.cantidad = Number($("cantidad").value);

  if (
    !Number.isInteger(reserva.cantidad) ||
    reserva.cantidad < 1 ||
    reserva.cantidad > 20
  ) {
    alert("Ingresá entre 1 y 20 entradas.");
    return;
  }

  try {
    // El backend crea una matriz temporal.
    const datos = await api("seleccion/iniciar", {
      funcion_id: reserva.funcion.id,
      cantidad: reserva.cantidad
    });

    reserva.sesion = datos.sesion;
    reserva.matriz = datos.matriz;
    reserva.seleccionadas = [];

    $("detalle-funcion").textContent =
      `${reserva.funcion.pelicula} - ` +
      `${reserva.funcion.fecha} ${reserva.funcion.hora}`;

    informar("mensaje-butacas", "");
    dibujarMapaCliente();
    mostrarPaso("paso-butacas");

  } catch (error) {
    controlarError(error);
  }
});

// ------------------------------------------
// 15. PASO 4 - DIBUJAR MATRIZ DE C++
// ------------------------------------------

// Estados:
// 0 = disponible
// 1 = ocupada previamente
// 2 = seleccionada por el cliente actual
// 3 = inhabilitada

function dibujarMapaCliente() {
  const contenedor = $("mapa-butacas");
  contenedor.replaceChildren();

  reserva.matriz.forEach((columnas, filaIndice) => {
    const fila = document.createElement("div");
    fila.className = "fila-butacas";

    const etiqueta = document.createElement("span");
    etiqueta.className = "etiqueta-fila";
    etiqueta.textContent =
      String.fromCharCode(65 + filaIndice);

    fila.append(etiqueta);

    columnas.forEach((estado, columnaIndice) => {
      const nombres = [
        "disponible",
        "ocupada",
        "seleccionada",
        "inhabilitada"
      ];

      const b = crearBoton(
        nombreButaca(filaIndice, columnaIndice),
        async () => {
          b.disabled = true;

          try {
            // Cada clic modifica la matriz de C++.
            const datos = await api("seleccion/marcar", {
              sesion: reserva.sesion,
              fila: filaIndice,
              columna: columnaIndice
            });

            reserva.matriz = datos.matriz;
            reserva.seleccionadas = datos.seleccionadas;

            informar("mensaje-butacas", "");
            dibujarMapaCliente();

          } catch (error) {
            controlarError(error, "mensaje-butacas");
            dibujarMapaCliente();
          }
        }
      );

      b.className = "butaca " + nombres[estado];

      b.disabled = estado === 1 || estado === 3;

      fila.append(b);
    });

    contenedor.append(fila);
  });

  $("contador-butacas").textContent =
    reserva.seleccionadas.length + " / " +
    reserva.cantidad;

  $("continuar-confirmacion").disabled =
    reserva.seleccionadas.length !== reserva.cantidad;
}

// ------------------------------------------
// 16. PASO 5 - CONFIRMAR RESERVACIÓN
// ------------------------------------------

$("continuar-confirmacion").onclick = () => {
  if (
    reserva.seleccionadas.length !== reserva.cantidad
  ) return;

  const butacas = reserva.seleccionadas.map(
    s => nombreButaca(s.fila, s.columna)
  );

  const resumen = $("resumen-reserva");
  resumen.replaceChildren();

  const datos = [
    ["Cliente", reserva.nombre],
    ["Teléfono", reserva.telefono],
    ["Película", reserva.funcion.pelicula],
    ["Fecha", reserva.funcion.fecha],
    ["Hora", reserva.funcion.hora],
    ["Sala", reserva.funcion.sala],
    ["Butacas", butacas.join(", ")],
    [
      "Total",
      dinero(reserva.cantidad * reserva.pelicula.precio)
    ]
  ];

  datos.forEach(([titulo, valor]) => {
    const p = document.createElement("p");
    const strong = document.createElement("strong");

    strong.textContent = titulo + ": ";
    p.append(strong, document.createTextNode(valor));
    resumen.append(p);
  });

  informar("mensaje-confirmacion", "");
  mostrarPaso("paso-confirmacion");
};

// GUARDAR EN SQLITE

$("confirmar-reserva").onclick = async () => {
  const b = $("confirmar-reserva");
  b.disabled = true;

  try {
    const resultado = await api("reservas", {
      sesion: reserva.sesion,
      nombre: reserva.nombre,
      telefono: reserva.telefono
    });

    informar(
      "detalle-confirmacion",
      "Reservación #" + resultado.reserva_id +
      " registrada correctamente. " +
      "Se guardaron " + resultado.cantidad + " butacas."
    );

    reserva.sesion = "";
    mostrarPaso("reserva-exitosa");

  } catch (error) {
    informar(
      "mensaje-confirmacion",
      error.message +
      " Volvé a seleccionar tus butacas."
    );

    reserva.sesion = "";
    $("volver-butacas").textContent =
      "Seleccionar nuevamente";

  } finally {
    b.disabled = false;
  }
};

// ------------------------------------------
// 17. BOTONES PARA REGRESAR
// ------------------------------------------

$("volver-cliente").onclick = () =>
  mostrarPaso("paso-cliente");

$("volver-funcion").onclick = () =>
  mostrarPaso("paso-funcion");

$("volver-cantidad").onclick = () =>
  mostrarPaso("paso-cantidad");

$("volver-butacas").onclick = () => {
  if (reserva.sesion) {
    mostrarPaso("paso-butacas");
  } else {
    mostrarPaso("paso-cantidad");
  }
};

$("regresar-inicio").onclick = async () => {
  $("form-cliente").reset();
  $("form-funcion").reset();
  $("form-cantidad").reset();

  reserva = {
    nombre: "",
    telefono: "",
    pelicula: null,
    funcion: null,
    cantidad: 0,
    sesion: "",
    matriz: [],
    seleccionadas: []
  };

  mostrarPaso("paso-cliente");

  await cargarDatos();
  location.hash = "inicio";
};
