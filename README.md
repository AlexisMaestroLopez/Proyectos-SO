# Sistemas Operativos - Prácticas y Proyectos (URJC)

Este repositorio reúne los proyectos prácticos obligatorios desarrollados en **C** para la asignatura de **Sistemas Operativos** del Grado en Ingeniería Informática en la Universidad Rey Juan Carlos (Curso 2025-26). 

Los proyectos se enfocan en la programación a bajo nivel en entornos UNIX/Linux, abarcando el ciclo de vida de los procesos, comunicación interprocesal, gestión de señales y sincronización avanzada de hilos en escenarios de alta concurrencia.

---

## Estructura del Repositorio

El repositorio se divide en dos proyectos principales independientes:

### 1. Shell Interactiva Personalizada (`myshell.c`)
Desarrollo de un intérprete de mandatos (**Minishell**) interactivo que se comunica directamente con el kernel de Linux utilizando la API estándar de POSIX. El programa lee líneas desde la entrada estándar, las procesa mediante una librería de análisis sintáctico (`parser.h`) y ejecuta las secuencias correspondientes.

* **Gestión de Procesos:** Clonación de procesos mediante `fork()`, reemplazo de imágenes de memoria con la familia `exec()`, y sincronización o recolección de estados de salida con `wait()` / `waitpid()`.
* **Tuberías (Pipes):** Ejecución de secuencias encadenadas de mandatos separados por `|` comunicando descriptores de archivo mediante `pipe()`.
* **Redirecciones de E/S:** Modificación de descriptores de archivos estándar (`dup2`) para soportar redirección de entrada (`< fichero`), salida (`> fichero`) y salida de error (`>& fichero`).
* **Control de Concurrencia (Background):** Soporte para ejecución en segundo plano (`&`), evitando el bloqueo del prompt principal. Implementación de comandos internos de control de trabajos: `jobs` (visualización de estados `Running` / `Stopped`) y `bg` (reanudación en segundo plano).
* **Comandos Internos (*Built-ins*):** Lógica nativa para comandos que modifican el contexto de la propia shell: `cd` (con soporte para rutas relativas, absolutas y la variable `HOME`), `umask` (máscara octal de permisos bitwise) y `exit` (salida ordenada).
* **Manejo Asíncrono de Señales:** Captura y tratamiento selectivo de `SIGINT` (Ctrl+C) y `SIGTSTP` (Ctrl+Z) para proteger al proceso padre de terminaciones inesperadas y gestionar la parada/continuación de los hijos en foreground.

### 2. Simulación Concurrente de una Cafetería (`cafeteria.c`)
Diseño e implementación de una simulación de eventos discretos multihilo enfocado en resolver un problema clásico de sincronización con recursos finitos. El programa modela el flujo de una cafetería con un número variable de clientes, camareros, asientos en barra y capacidad límite en la cola de pedidos.

* **Paradigma Multihilo:** Uso intensivo de la librería **POSIX Threads (`pthread.h`)** donde cada cliente y cada camarero actúan como hilos de ejecución independientes.
* **Sincronización Avanzada:** Exclusión mutua implementada mediante cerrojos (**Mutexes**) y control de flujo mediante **Variables de Condición** (`pthread_cond_t`) o semáforos, garantizando un entorno **libre de interbloqueos (*deadlocks*)** e **inanición (*starvation*)**.
* **Sección Crítica:** Gestión coordinada de recursos compartidos (sillas libres en la barra y cola estructurada de pedidos).
* **Configuración Dinámica:** Lectura parametrizada del entorno mediante ficheros de texto (número de hilos, límites de capacidad y rangos de tiempo aleatorios calculados con `rand()`).
* **Persistencia de Logs:** Volcado simultáneo en tiempo real de los estados de la simulación tanto por la salida estándar como en archivos estructurados con marcas de tiempo (`sim_cafeteria_<fecha_hora>.txt`).

---

## Compilación y Ejecución (Entornos Linux / WSL)

Ambos proyectos han sido desarrollados cumpliendo con estrictas normas de conformidad ANSI C, evitando construcciones de C++ y optimizados para compilarse sin *warnings* bajo banderas de depuración estrictas (`-Wall -Wextra`).

### Compilar y Ejecutar la MiniShell
Para compilar la shell, es necesario enlazar de forma estática la librería del analizador sintáctico proporcionada (`libparser.a`):
```bash
gcc -Wall -Wextra myshell.c libparser.a -o myshell -static
./myshell
