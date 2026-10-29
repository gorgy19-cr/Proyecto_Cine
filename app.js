
/* ==========================================
   CINEPLUS - CONTROL DE ACCESO Y NAVEGACIÓN
   ========================================== */

// Obtener los elementos del HTML

const pantallaAcceso =
  document.getElementById("pantalla-acceso");

const aplicacion =
  document.getElementById("aplicacion");

const administracion =
  document.getElementById("administracion");

const navAdministracion =
  document.getElementById("nav-administracion");

const seccionReserva =
  document.getElementById("reservar");

const navReserva =
  document.getElementById("nav-reservar");

const botonReservarInicio =
  document.getElementById("boton-reservar-inicio");

const navInicio =
  document.getElementById("nav-inicio");


// FUNCIÓN PARA INGRESAR AL SISTEMA

function ingresar(rol) {

  const esAdmin = rol === "administrador";

  // Ocultar la pantalla de bienvenida
  pantallaAcceso.hidden = true;

  // Mostrar la aplicación
  aplicacion.hidden = false;

  // Mostrar administración solo al administrador
  administracion.hidden = !esAdmin;
  navAdministracion.hidden = !esAdmin;

  // Mostrar reservaciones solo al cliente
  seccionReserva.hidden = esAdmin;
  navReserva.hidden = esAdmin;

  // Ocultar el botón de reservar al administrador
  botonReservarInicio.hidden = esAdmin;

  // Dirigir a la sección correspondiente
  location.hash = esAdmin
    ? "administracion"
    : "inicio";

}


// BOTÓN: INGRESAR COMO CLIENTE

document.getElementById("entrar-cliente")
  .addEventListener("click", function() {

    ingresar("cliente");

  });


// BOTÓN: INGRESAR COMO ADMINISTRADOR

document.getElementById("entrar-admin")
  .addEventListener("click", function() {

    ingresar("administrador");

  });


// BOTÓN: CAMBIAR DE ACCESO

document.getElementById("cambiar-rol")
  .addEventListener("click", function() {

    // Ocultar aplicación
    aplicacion.hidden = true;

    // Volver a la pantalla inicial
    pantallaAcceso.hidden = false;

    // Ocultar administración
    administracion.hidden = true;
    navAdministracion.hidden = true;

    // Limpiar navegación
    history.replaceState(
      null,
      "",
      location.pathname + location.search
    );

    // Regresar arriba
    window.scrollTo(0, 0);

  });

const formularioSala = document.getElementById("form-sala");

formularioSala.addEventListener("submit", async function(evento) {
  evento.preventDefault();

  const datos = Object.fromEntries(new FormData(formularioSala));

  try {
    const respuesta = await fetch("http://localhost:3000/api/salas", {
      method: "POST",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify(datos)
    });

    if (!respuesta.ok) {
      throw new Error("El servidor no pudo registrar la sala.");
    }

    alert("Sala registrada correctamente.");
    formularioSala.reset();
  } catch (error) {
    alert("No se pudo conectar con el servidor C++.");
    console.error(error);
  }
});