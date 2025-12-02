# SaturnControl (C++ Cassini Port)

**SaturnControl** is a native C++ GUI application designed to control and monitor Elegoo Saturn 3D printers over the network. 

This project is a high-performance port of the original Python tool **[Cassini](https://github.com/vladimirv/cassini)**. It replicates the complex network logic (custom MQTT broker + HTTP server handshake) required to communicate with the printer's mainboard, but wrapped in a modern, easy-to-use Qt 6 interface.

## Features

*   **Network Discovery:** Automatically finds Saturn printers on the local network via UDP broadcast.
*   **Status Monitoring:** Real-time feedback on printer status (Idle, Printing, Busy), current layer, and total layers.
*   **File Upload & Print:** Allows uploading `.goo` or `.ctb` files directly to the printer and starting the print job immediately.
*   **Multi-language Support:** The user interface is available in English and Spanish, with a language selector on the main screen.
*   **Native Performance:** Built with C++17 and Qt 6 for minimal resource usage and zero Python dependencies on the client machine.

## Prerequisites

*   Linux, Windows, or macOS.
*   **CMake** (3.16 or higher).
*   **Qt 6** (Core, Network, Widgets, LinguistTools modules).
*   C++ Compiler supporting C++17.

### Installing Dependencies (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools qt6-l10n-tools
```

## Build Instructions

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/yourusername/SaturnControl.git
    cd SaturnControl
    ```

2.  **Create a build directory and run CMake:**
    ```bash
    mkdir build
    cd build
    cmake ..
    ```

3.  **Update translations (Optional):**
    If you have modified any translatable strings in the source code, update the `.ts` file with Qt's `lupdate` tool. You can then use Qt Linguist to add the translations.
    ```bash
    lupdate .. -ts ../translations/saturn_es.ts
    ```

4.  **Compile the project:**
    ```bash
    make
    ```

5.  **Run the application:**
    ```bash
    ./SaturnControl
    ```

## Credits & License

*   **Original Logic (Cassini):** Copyright (C) 2023 Vladimir Vukicevic (MIT License). This C++ port bases its reverse-engineered network protocol logic on his work.
*   **C++ Port:** Developed as an open-source contribution.

---

# SaturnControl (Versión en Español)

**SaturnControl** es una aplicación nativa en C++ con interfaz gráfica diseñada para controlar y monitorizar impresoras 3D Elegoo Saturn a través de la red local.

Este proyecto es un *port* de alto rendimiento de la herramienta original en Python llamada **[Cassini](https://github.com/vladimirv/cassini)**. Replica la compleja lógica de red (*Broker* MQTT personalizado + *Handshake* de servidor HTTP) necesaria para comunicarse con la placa base de la impresora, pero envuelta en una interfaz moderna y fácil de usar basada en Qt 6.

## Características

*   **Descubrimiento de Red:** Encuentra automáticamente impresoras Saturn en la red local mediante broadcast UDP.
*   **Monitorización de Estado:** Información en tiempo real del estado de la impresora (En espera, Imprimiendo, Ocupada), capa actual y total de capas.
*   **Subida e Impresión:** Permite subir archivos `.goo` o `.ctb` directamente a la impresora e iniciar el trabajo de impresión inmediatamente.
*   **Soporte Multi-idioma:** La interfaz de usuario está disponible en inglés y español, con un selector de idioma en la pantalla principal.
*   **Rendimiento Nativo:** Construido con C++17 y Qt 6 para un uso mínimo de recursos y sin dependencias de Python en la máquina cliente.

## Requisitos Previos

*   Linux, Windows o macOS.
*   **CMake** (3.16 o superior).
*   **Qt 6** (Módulos Core, Network, Widgets, LinguistTools).
*   Compilador C++ con soporte para C++17.

### Instalación de dependencias (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools qt6-l10n-tools
```

## Instrucciones de Compilación

1.  **Clona el repositorio:**
    ```bash
    git clone https://github.com/tuusuario/SaturnControl.git
    cd SaturnControl
    ```

2.  **Crea un directorio de compilación y ejecuta CMake:**
    ```bash
    mkdir build
    cd build
    cmake ..
    ```
    
3.  **Actualizar traducciones (Opcional):**
    Si has modificado alguna de las cadenas de texto traducibles en el código fuente, actualiza el archivo `.ts` con la herramienta `lupdate` de Qt. Después puedes usar Qt Linguist para añadir las traducciones.
    ```bash
    lupdate .. -ts ../translations/saturn_es.ts
    ```

4.  **Compila el proyecto:**
    ```bash
    make
    ```

5.  **Ejecuta la aplicación:**
    ```bash
    ./SaturnControl
    ```

## Créditos y Licencia

*   **Lógica Original (Cassini):** Copyright (C) 2023 Vladimir Vukicevic (Licencia MIT). Este port en C++ basa su lógica de protocolo de red e ingeniería inversa en su trabajo.
*   **Port C++:** Desarrollado como contribución de código abierto.
