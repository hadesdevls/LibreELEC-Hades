# LibreELEC Hades

[English](README.md) | **Español**

LibreELEC Hades es una compilación personal y no oficial de [LibreELEC](https://libreelec.tv) para PC corrientes de 64 bits (la imagen "Generic x86_64"). Es LibreELEC con un añadido principal: su centro multimedia, que es Kodi® con cambios, puede enviar vídeo Dolby Vision a una tele Dolby Vision por HDMI desde la gráfica del PC, sea una tarjeta gráfica o la integrada en el procesador, en vez de quedarse en HDR10. Todo lo demás que hace LibreELEC se conserva. Nació del fork de LibreELEC con Dolby Vision para gráficas Intel de CroqueMr; mira "Créditos".

> **Esto no es LibreELEC y no es oficial.** LibreELEC Hades no lo hacen, mantienen, avalan ni certifican Team LibreELEC, Team Kodi o la XBMC Foundation, Dolby Laboratories, ni NVIDIA, AMD o Intel, y no tiene relación con ninguno de ellos. Por favor, no pidas ayuda sobre Hades a esos proyectos ni a esas empresas. Kodi es una marca registrada de la XBMC Foundation. Las demás marcas son de sus dueños; consulta [NOTICE.es.md](NOTICE.es.md).

**Por qué la pantalla dice "LibreELEC Hades" y no "Kodi".** El centro multimedia de esta compilación es un Kodi modificado que la XBMC Foundation no ha aprobado, y la política de marca de Kodi pide a una compilación así que no use como suyos el nombre y los logos de Kodi. Por eso la pantalla de arranque, el logo, los iconos y los textos que nombraban el programa dicen LibreELEC Hades, y una página de la información del sistema, "Acerca de esta compilación", dice qué es y dónde apoyar a Kodi y a LibreELEC. Por debajo es Kodi: complementos, interfaces y aplicaciones de mando a distancia funcionan como con Kodi, su registro sigue siendo `kodi.log`, y lo que leen otros programas (el agente de usuario, JSON-RPC, los nombres de DLNA y AirPlay) sigue diciendo Kodi. En este documento, "Kodi" es ese programa.

**Estado: versión 1.0, candidata 1 (octubre de 2026). Experimental.** Lo hace una sola persona y se ha probado en muy poco hardware. Lee "Qué se ha probado" antes de esperar que funcione en el tuyo.

## Qué añade a LibreELEC

- **Dolby Vision por HDMI desde un PC.** La imagen se reconstruye en el PC, incluida la capa extra de los discos de "perfil 7" (FEL), y se envía a la tele con su información Dolby Vision de cada fotograma. La tele hace después su propio procesado Dolby Vision. Es el modo que las teles llaman "Standard" o "TV-led".
- **Gráficas de NVIDIA, AMD e Intel,** tanto tarjetas como las integradas en el procesador (NVIDIA, con sus módulos de kernel abiertos).
- **Nunca te deja a propósito con la pantalla en negro.** Dolby Vision solo se usa cuando el vídeo, la tele, el enlace HDMI y el controlador gráfico cumplen. Ante cualquier duda, el vídeo se reproduce como en Kodi normal (HDR10 o SDR).
- **La frecuencia es la de Kodi.** Dolby Vision nunca cambia la frecuencia de refresco que Kodi elegiría. El vídeo progresivo recibe su frecuencia exacta (25 fps a 25 Hz, 29,97 fps a 29,97 Hz). En NVIDIA también se ofrecen los modos a 24, 30, 60 y 120 Hz exactos: el controlador de NVIDIA ofrecía muchos de los modos de una tele solo a 23,976, 29,97, 59,94 o 119,88 Hz, así que un vídeo a 24 fps exactos salía a 60 Hz o en un modo de 4096 píxeles de ancho.
- **Margen para las gráficas lentas.** Donde dibujar un fotograma Dolby Vision ocupa casi todo un periodo de refresco (gráficas integradas con el perfil 7), el fotograma se prepara un refresco antes, y de las dos formas de dibujarlo se mide y se recuerda la más rápida en cada gráfica.
- **Dolby Vision mostrado como HDR10** en las salidas que no lo admiten nativo: por defecto solo para el perfil 5, cuyo vídeo base no es una imagen utilizable por sí sola, y para el perfil 7 si lo activas (*Sistema > Pantalla*). El resultado es HDR10 sin mapeo de tonos.
- **PC con más de una gráfica, y portátiles.** Kodi arranca en la tele, esté conectada a la gráfica que esté, y no en la primera pantalla que encuentra. Si la tele se conecta cuando Kodi ya ha arrancado, Kodi ofrece pasarse a ella.
- **3D estereoscópico (nuevo en esta versión, y nunca probado en una pantalla 3D).** Las dos vistas del vídeo de Blu-ray 3D (H.264 MVC) se decodifican cuando la pantalla dice que admite 3D, de un disco, de su imagen o su carpeta y de un archivo Matroska hecho de uno, y una tele o un proyector 3D se pone en 3D solo por HDMI: en "frame packing" donde la pantalla y el controlador gráfico lo tienen, y si no en el formato de media resolución que se dibuja. Mira "Límites conocidos".
- **Avisos que explican lo que pasa:** cuando la entrada de la tele solo ofrece Dolby Vision de "baja latencia", cuando la entrada de la tele está en un modo de compatibilidad HDMI 1.4, cuando no se ve ninguna tele, y una página Dolby Vision en la información del reproductor.
- **Valores por defecto más seguros** para un PC que además tiene tus otros discos (más abajo), y un pequeño instalador para Windows.
- **Un interruptor de diagnóstico.** Apagado por defecto. Encendido (una casilla del instalador), cada arranque guarda en la propia unidad un registro filtrado y de tamaño limitado de lo que vio el sistema, para acompañar a un aviso de fallo.

## Qué necesitas

- Un PC de 64 bits. **Secure Boot tiene que estar desactivado**: Hades todavía no arranca con él.
- Una memoria USB o un disco USB de 8 GB o más. Con un SSD por USB va mucho más fluido que con una memoria.
- Para Dolby Vision:
  - **Una tele que anuncie Dolby Vision "Standard" (TV-led) en la entrada HDMI que uses.** Muchas teles solo anuncian Dolby Vision de "baja latencia" en una entrada configurada para juegos o para PC; a una entrada así Hades no le envía Dolby Vision nativo, y te lo dice. En la única tele usada en las pruebas (una LG C5) hay que tener "Quick Frame Transport" desactivado en esa entrada.
  - **Una gráfica para la que Hades tenga salida Dolby Vision,** sea una tarjeta o la integrada en el procesador:
    - NVIDIA GeForce GTX serie 16, RTX serie 20 o posteriores. Las NVIDIA anteriores (GTX serie 10 y más antiguas) no las admite la rama del controlador que usa Hades.
    - Gráficas AMD Radeon de generaciones recientes (el hardware de pantalla antiguo, "DCN 1" y "DCN 2", no está cubierto).
    - Gráficas Intel, con la salida Dolby Vision del fork de CroqueMr, del que partió este trabajo.
  - **El PC conectado por HDMI directamente a la tele.** Con una barra de sonido o un receptor AV entre el PC y la tele no se ha probado.
- Un vídeo Dolby Vision. Hades no trae películas, ni complementos, ni fuentes de contenido.

## Qué se ha probado

Todas las pruebas las hizo el autor con las builds de prueba que llevaron a esta versión: en su PC, un AMD Ryzen 9 7950X3D con una NVIDIA GeForce RTX 4080 SUPER, tanto en la tarjeta NVIDIA como en la gráfica Radeon integrada del procesador, con una tele LG C5 y el sonido por una barra TCL Q85H Pro (en el eARC de la tele), con el sistema en un SSD USB HIKSEMI HS-USB-SYNC C PRO de 128 GB; y en un portátil con un Intel Core i5-12450H. En NVIDIA, con el controlador de esta versión, 615.78.08. "Funciona" quiere decir visto funcionar en ese hardware, con colores correctos y con una vuelta limpia a los menús; "medido" añade que en los registros se comprobaron los metadatos de cada fotograma y que no se perdieron fotogramas. El logotipo Dolby Vision de la tele, por sí solo, no cuenta como prueba. "Sin probar" quiere decir exactamente eso.

| Qué | Estado |
| --- | --- |
| Dolby Vision perfil 7 con la capa de mejora completa (FEL) | Funciona, medido: RTX 4080 SUPER, y la gráfica Radeon integrada del Ryzen 9 7950X3D (240 fotogramas cada 10 s, ninguno perdido, la capa de mejora en todos; mira "Límites conocidos"). Funciona: la gráfica integrada del Core i5-12450H |
| Dolby Vision perfil 8.1 | Funciona, medido: RTX 4080 SUPER. Otras gráficas: **sin probar** |
| Dolby Vision perfil 5 | Funciona, medido: RTX 4080 SUPER. Otras gráficas: **sin probar** |
| Dolby Vision en dos pistas de vídeo (estructura `.m2ts` de Blu-ray) | Funciona: RTX 4080 SUPER |
| Dolby Vision sin ajustar la frecuencia de refresco al vídeo (60 Hz) | Funciona, medido: RTX 4080 SUPER |
| Salir de Dolby Vision con los menús a 120 Hz | **Sin probar** con esta versión, que arregla una pérdida de imagen que ahí tenían en NVIDIA las builds de prueba anteriores |
| Dolby Vision mostrado como HDR10 donde no cabe la salida nativa | Ejecutado en la RTX 4080 SUPER con un vídeo de perfil 5; comprobado por software |
| Vídeo AV1 con Dolby Vision | **Sin probar** (no hay muestra con que probarlo) |
| HDR10 a más de 8 bits en NVIDIA | Funciona: RTX 4080 SUPER |
| Un vídeo a 24 fps exactos enviado a 24 Hz exactos en NVIDIA (modos que esta versión añade a los que ofrece el controlador) | Funciona, medido: RTX 4080 SUPER |
| Dolby TrueHD con Atmos, en paso directo a la barra de sonido | Funciona: RTX 4080 SUPER, y las gráficas integradas del Ryzen 9 7950X3D y del Core i5-12450H |
| Dolby Digital Plus con Atmos, en paso directo a la barra de sonido | Funciona: RTX 4080 SUPER |
| El sonido sigue cuando cambian la pantalla y la salida de audio (el sonido sigue a la pantalla) | Funciona, medido: el PC del autor, de la tarjeta NVIDIA a la gráfica del procesador y al revés |
| Un PC o un portátil con más de una gráfica: Kodi arranca en la tele, esté conectada a la que esté | Funciona en el PC del autor: la tele en la tarjeta NVIDIA, con la gráfica del procesador y su propio puerto HDMI también presentes. Otras combinaciones, sin probar |
| Pasarse a una tele que se conecta después de arrancar Kodi | **Sin probar** |
| Otras gráficas: NVIDIA GTX 16, RTX 20, 30 y 50; otras gráficas AMD e Intel | **Sin probar** |
| Teles distintas de la LG C5; teles antiguas con un bloque Dolby Vision de "versión 1" | **Sin probar** |
| 3D estereoscópico: decodificar las dos vistas y poner una pantalla en 3D | **Sin probar en ningún hardware.** El decodificador da las imágenes del decodificador de referencia en los flujos 3D con que se comprobó, salvo en uno retirado (mira "Notas técnicas"); lo demás solo se ha leído y compilado |
| 3D de un disco, de su imagen o de su carpeta | **Sin probar: no había ningún disco 3D a mano.** Lo que lee los dos clips de un disco se ha leído y compilado, y su regla para retomar la segunda vista tras un hueco se midió con flujos hechos para ello |
| Arrancado desde USB: discos internos ocultos | Funciona, medido |
| Instalado en un disco interno: los demás discos del PC ocultos | Solo en una máquina virtual |
| Entrada de arranque que muestra los discos internos en solo lectura, y el ajuste que hace de ella la predeterminada | Solo en una máquina virtual |
| Instalador para Windows | "Actualizar" se ha ejecutado en un disco USB real. Todas las operaciones (borrar, hacer sitio, particionar, actualizar, quitar) pasan la prueba del propio instalador en un disco virtual, y lo que escribe arranca en una máquina virtual. En un disco interno real, **ninguna se ha ejecutado** |

SDR, HDR10, HLG, el paso directo de audio, los menús y el cambio de frecuencia de refresco deben comportarse como en LibreELEC. Usan el código normal de Kodi.

## Límites conocidos

- Dolby Vision necesita una señal HDMI exacta, sin tocar. La frecuencia de refresco variable, el escalado y los ajustes de color del controlador gráfico no son compatibles con ella.
- En las gráficas integradas de AMD, el perfil 7 (FEL) está cerca del límite del chip. El primer vídeo Dolby Vision tras instalar una build nueva se usa para medir cuál de las dos formas de dibujar es más rápida ahí, y puede dar algún tirón corto mientras se mide; lo encontrado se conserva para los vídeos siguientes.
- Sin Secure Boot.
- **El 3D es nuevo y está sin demostrar.**
  - Nada de él se ha ejecutado en una gráfica ni se ha visto en una pantalla 3D. Si un vídeo 3D se ve negro o roto, desactiva *Sistema > Pantalla > Poner la pantalla en 3D (HDMI)*; las dos mitades salen entonces como una imagen normal y la pantalla se pone en 3D a mano, como en LibreELEC.
  - Las dos vistas se decodifican de un disco Blu-ray 3D, de su imagen o su carpeta, y de un archivo Matroska (`.mkv`) hecho de uno. No había ningún disco 3D a mano: el disco no se ha probado nunca. Donde la segunda vista de un disco no se puede leer o no coincide con la primera, la película sigue en 2D y el registro dice por qué; tras un hueco así, la segunda vista vuelve en la siguiente imagen desde la que se puede decodificar limpia, lo que puede tardar un segundo o dos. Un vídeo 3D entrelazado en un archivo, y un archivo que guarda lo que describe la segunda vista fuera del propio vídeo (algunos MP4), se reproducen en 2D, como en LibreELEC.
  - Las dos vistas las decodifica el procesador, por software, no el decodificador de vídeo de la gráfica.
  - El "frame packing", que conserva los dos ojos a resolución completa, existe en HDMI para 1080p a 24 Hz: para una película tiene que estar activado el ajuste de la frecuencia de la pantalla al vídeo de Kodi. Habrá un cambio de modo de pantalla más cuando un vídeo 3D empieza y cuando termina.
  - La pantalla tiene que estar encendida cuando Kodi arranca, o Kodi no se entera de que admite 3D.
  - El "frame packing" en NVIDIA depende de algo que no se pudo averiguar sin el hardware (si su controlador da a Kodi un búfer del tipo que la salida necesita). Si no lo da, Kodi usa el formato de media resolución.
  - No uses la calibración de pantalla de Kodi mientras se muestra una imagen 3D en "frame packing": sus controles son para el modo normal, y lo que se cambie ahí no se guarda.
  - Con una gráfica NVIDIA, una pantalla que anuncia 3D por HDMI se queda sin HDR10. Es una regla del controlador de NVIDIA, con Hades o sin él.
  - Con una pantalla que no dice que admite 3D, un vídeo 3D se reproduce en 2D como antes, salvo que pongas *Reproductor > Vídeos > Decodificar las dos vistas del vídeo de Blu-ray 3D (MVC)* en "Siempre" (para gafas anaglifo, por ejemplo).
- Es una implementación independiente escrita a partir de una especificación pública. Dolby no la ha probado ni certificado, y puede diferir de un reproductor certificado.

## Valores por defecto distintos de los de LibreELEC

No se quita nada de LibreELEC. Lo que es inseguro por defecto en un PC con otros discos se pone en un valor seguro y se puede volver a activar.

- **Los discos de tu PC.** Arrancado desde USB (las entradas "Run" y "Live"), Hades no inicia los controladores NVMe y SATA, así que los discos internos no existen para él. Otra entrada de arranque, "Run (internal disks read-only)", los muestra, en solo lectura. Instalado en un disco interno con el instalador para Windows, Hades tampoco inicia los demás discos NVMe y SATA del PC, y no monta las demás particiones de su propio disco. Las unidades USB y las tarjetas SD se montan como siempre. Un interruptor en los ajustes de LibreELEC (*Sistema > Discos internos > Mostrar los discos internos (solo lectura)*) hace que la entrada que los muestra sea la que el menú de arranque inicia sola. Está apagado por defecto; explica lo que supone y pregunta antes de encenderse. Mostrados, los discos los puede leer todo lo que funciona en Kodi, complementos incluidos; "solo lectura" es cómo se montan, no un cerrojo; y esa entrada arranca el kernel sin su bloqueo. Elegir "Run" una vez en el menú de arranque vuelve a apagar el interruptor, y también lo apagan un restablecimiento a los valores por defecto y una actualización hecha con el instalador.
- **La entrada del instalador** está, pero la imagen arranca en "Run", no en el instalador.
- **Bloqueo del kernel.** Las entradas que ocultan los discos arrancan además el kernel en bloqueo de "integridad": solo carga los módulos firmados para esa build.
- **Samba (compartir archivos) está apagado.** Al encenderlo pide una contraseña que Hades genera y muestra en los ajustes. Compartir sin contraseña sigue pudiéndose elegir.
- **Actualizaciones.** Hades no contacta con el servidor de actualizaciones de LibreELEC, porque una actualización oficial de LibreELEC quitaría Dolby Vision. No hay comprobación automática: se actualiza a mano. Los archivos de actualización que no son de Hades se rechazan. Por defecto no se envían estadísticas de uso.
- **Red.** El cortafuegos arranca en modo "casa" en una instalación nueva. AirPlay está apagado. SSH está apagado, como en LibreELEC, y mientras la contraseña de root sea la de fábrica solo acepta claves, no contraseñas.
- **Bluetooth** usa reglas de emparejamiento más estrictas, con una opción para dispositivos de entrada antiguos.
- **Reloj.** El reloj de la placa se deja en hora local y no se escribe nunca, para que Windows conserve la hora correcta en un PC que arranca los dos sistemas.
- **Los programas de 32 bits** están apagados por defecto (`ia32_emulation=1` en la línea del kernel los activa).

## Qué no incluye

- **Ni complementos de terceros ni fuentes de contenido.** La imagen trae los complementos estándar de Kodi, el repositorio de complementos de Kodi y el de LibreELEC, como LibreELEC. Las únicas carpetas configuradas en Kodi son las locales y vacías de LibreELEC (Vídeos, Series, Música, Imágenes). Lo que instales es cosa tuya.
- **Nada de software de Dolby.** En Hades no hay código, bibliotecas ni claves de licencia de Dolby.
- **Nada de telemetría.** Hades no añade ninguna y, por defecto, no envía estadísticas de uso ni ningún identificador de tu PC. Como LibreELEC y Kodi, sigue contactando con los servicios que ellos usan (repositorios de complementos, servidores de hora y similares): la página "Política de privacidad" de la información del sistema empieza con una nota que los enumera (en inglés). El registro de diagnóstico, cuando lo enciendes, se queda en tu unidad: nada lo envía a ningún sitio.

## Instalar y arrancar

Necesitas un archivo de imagen (`LibreELEC-Generic.x86_64-...img.gz`): de las publicaciones del repositorio en el que está este archivo, o compilado por ti.

1. Escribe la imagen en una unidad USB, con el instalador de Hades para Windows (`LibreELEC-Hades-Installer.exe`, cuyo propio LEEME explica cada paso) o con cualquier programa que escriba una imagen de disco en una unidad USB. La unidad se borra.
2. Reinicia el PC, abre el menú de arranque del firmware (F8, F11, F12 o Esc en la mayoría de los PC) y elige la unidad USB.
3. El primer arranque prepara el almacenamiento y reinicia una vez.

Para volver a tu sistema de siempre, apaga Hades y enciende el PC con normalidad. Arrancado desde USB, Hades no cambia los ajustes de arranque del PC ni nada de Windows.

## Actualizar

- Con el instalador para Windows: elige la unidad que ya tiene Hades y "Actualizar". Los ajustes y los complementos se conservan.
- O como en LibreELEC: copia el archivo `.tar` de la build nueva a la carpeta `Update` y reinicia.

## Compilar desde el código fuente

A grandes rasgos; para todo lo que aquí no se dice vale la documentación de LibreELEC.

1. Usa una máquina Linux (el autor usa Ubuntu 26.04; LibreELEC mantiene recetas de contenedor en `tools/docker/`), un usuario sin privilegios, una ruta sin espacios y unos 75 GB de disco libre.
2. Obtén este repositorio: `main` es la versión actual, y el código fuente de una build es el commit que nombra su `BUILD_ID`.
3. Compila: `PROJECT=Generic DEVICE=Generic ARCH=x86_64 make image`
4. Los resultados aparecen en `target/`: un `.img.gz` (para una unidad USB), un `.tar` (para actualizar) y un `.ova` (máquina virtual).

Cosas que conviene saber:

- La compilación descarga el código fuente de cada paquete de la dirección de su receta (`package.mk`) y lo comprueba contra el SHA-256 escrito en ella. Ninguna receta tiene el hash vacío, y no se usan los paquetes "reusable" precompilados de la cadena de herramientas (`USE_REUSABLE="no"`).
- El paquete del controlador de NVIDIA se descarga de NVIDIA y se desempaqueta sin ejecutarlo. Solo se usan, sin cambios, sus bibliotecas de espacio de usuario y su firmware; los módulos del kernel se compilan del código abierto de NVIDIA. Al compilar aceptas la licencia de NVIDIA para esos archivos.
- Las builds del autor se hacen sin acceso a la red, a partir de fuentes descargadas y verificadas de antemano. Una compilación limpia tarda entre veinte y cuarenta y cinco minutos en una máquina de 32 hilos; mucho más en una pequeña.
- **Si cambias el árbol y compartes tu build, ponle un nombre tuyo** (`BUILDER_NAME=<tu nombre> make image`) y di que es tuya. Sin él, la build se llama a sí misma "Hades 1.0 RC1".

El instalador para Windows es un programa pequeño aparte (C#, en `tools/hades-installer/`); `build.ps1` lo compila con el compilador que Windows ya trae.

## Notas técnicas

La [documentación](docs/README.md), en inglés, cuenta más: cómo funciona la salida Dolby Vision, cada ajuste de esta build y el diagnóstico.

- **Señal.** Dolby Vision "Standard" se envía como una señal HDMI RGB de 8 bits y rango completo que transporta vídeo YCbCr 4:2:2 de 12 bits con los metadatos de gestión de pantalla en los bits bajos del croma, más un InfoFrame específico de Dolby. El transporte sigue la especificación pública ETSI GS CCM 001 V1.1.1. La salida tiene que ser exacta al bit: sin conversión de color, tabla de consulta, tramado, escalado, refresco variable ni DSC.
- **Parte común:** FFmpeg lee los metadatos Dolby Vision (RPU), libplacebo reconstruye la imagen y añade la capa de mejora, y un módulo de Kodi, `dvbridge`, empaqueta y presenta el fotograma.
- **Una salida por controlador,** detectada por propiedades DRM: Intel `i915` (parches del kernel del fork de CroqueMr), AMD `amdgpu` (un parche del kernel de este proyecto; necesita un enlace HDMI TMDS) y NVIDIA `nvidia-drm` con los módulos de kernel abiertos (la propiedad de InfoFrame del controlador, 8 bits por componente y tramado apagado).
- **Qué teles cumplen:** un EDID con un bloque Dolby Vision de versión 2 y la interfaz "Standard", o de versión 1 (12 bytes) con gestión de pantalla 4 o posterior; en NVIDIA la tele tiene además que permitir elegir el rango de cuantización.
- **3D, la decodificación.** FFmpeg solo decodifica la vista base de H.264 MVC. Hades compila un segundo decodificador H.264, `h264_mvc`, a partir de una copia del decodificador de FFmpeg con la serie multivista de Dom Cobley aplicada (pull request 24303 de FFmpeg, sin fusionar); el decodificador H.264 propio de FFmpeg queda byte a byte como está, y Kodi pide el segundo por su nombre, solo para un flujo que tiene segunda vista. Comprobado en una compilación de host contra el decodificador de referencia de H.264 (JM): los 27 flujos de conformidad MVC de ITU-T H.264.1 (vista base idéntica en todos; segunda vista idéntica en 18 de los 19 para los que el decodificador de referencia da una, y la excepción es un flujo retirado), y dos flujos estéreo hechos para ello, idénticos en las dos vistas (1920x1080, y 960x540, que da la vuelta al número de imagen de su segunda vista, cosa que un disco hace continuamente y ningún flujo de conformidad hace). La serie se toma tal como estaba el 2026-10-14, llevada a FFmpeg 9.0.2, con dos arreglos de este proyecto.
- **3D, los discos.** En un Blu-ray 3D la segunda vista está en un clip aparte. Lo que lee los dos clips a la vez es el demultiplexador de Dom Cobley para Kodi, tal como él lo escribió, con cambios de este proyecto alrededor: el segundo clip solo se lee donde se quieren las dos vistas, la lista de reproducción dice qué clip y qué ángulo, y tras una imagen que pasó sin su segunda vista no se da nada de ella al decodificador antes de una imagen desde la que se puede decodificar limpia (una imagen ancla).
- **3D, la salida.** Una pantalla sabe cómo viene una imagen 3D por el InfoFrame HDMI del fabricante, que los controladores envían para un modo que lleva ese formato en sus indicadores. El controlador de Intel tiene esos modos. Para AMD, el kernel lleva los parches de `amdgpu` del árbol de LibreELEC de cinema-ONE con arreglos de este proyecto; para NVIDIA, un parche de este proyecto a los módulos de kernel abiertos para el "frame packing", y la propiedad de InfoFrame del controlador para los formatos de media resolución. Un modo 3D nunca es una de las resoluciones de Kodi: es cómo se envía la resolución que Kodi eligió mientras se dibuja una imagen 3D, a la misma frecuencia de refresco.
- **Versiones en 1.0 RC1:** Linux 7.2.10, Kodi 22 (la rama `Piers` posterior a 22.0rc1), FFmpeg 9.0.2, Mesa 26.2.5, libplacebo de su rama principal, NVIDIA 615.78.08.
- **Basado en** LibreELEC `master` (13.0 en desarrollo). LibreELEC no ha publicado la versión 13.

## Créditos

Lo que esta build añade a LibreELEC se escribió para ella, salvo lo que se nombra aquí, que es trabajo de otros y se usa con agradecimiento:

- **El fork de LibreELEC con Dolby Vision de CroqueMr, y quienes colaboran en él** ([CroqueMr/libreelec-x86-DV](https://github.com/CroqueMr/libreelec-x86-DV)), donde empezó este trabajo. Tomados de él tal como los publicó: su módulo de Dolby Vision para Kodi (`dvbridge`), del que creció la salida Dolby Vision de esta build; su salida de Dolby Vision para gráficas Intel en el kernel de Linux; y sus cambios en FFmpeg y libplacebo, entre ellos un escalado más rápido para el perfil 7 de una versión posterior. Tres cosas de esta build parten de ideas de sus versiones posteriores y se escribieron de nuevo aquí: las bandas negras escritas antes de reconstruir la imagen (perfil 7 más rápido), las capturas durante Dolby Vision y Dolby Vision mostrado como HDR10.
- **Dom Cobley (popcornmix)**: la decodificación de la segunda vista del vídeo de Blu-ray 3D (su serie de cambios al decodificador H.264 de FFmpeg) y cómo lee Kodi los dos clips de un disco 3D, tal como él los escribió.
- **cinema-ONE** ([cinema-ONE/LibreELEC.tv](https://github.com/cinema-ONE/LibreELEC.tv)): los cambios al controlador del kernel de AMD que ponen una pantalla en 3D por HDMI, sobre los que se construye la salida 3D de esta build para AMD.
- **quietvoid** ([dovi_tool](https://github.com/quietvoid/dovi_tool)): una referencia consultada para los metadatos de Dolby Vision; en esta build no hay código suyo.
- **Team LibreELEC y Team Kodi**, y los autores de cada programa de la imagen. Los parches tomados de otros proyectos conservan a sus autores en su cabecera, y el único commit de otro autor que esta build toma tal cual (OpenSSL 4, de Rudi Heitbaum, en LibreELEC) conserva a su autor en el historial de este repositorio.

[NOTICE.es.md](NOTICE.es.md) los enumera con sus licencias.

## Licencias y código fuente

- El sistema de compilación de LibreELEC está bajo la Licencia Pública General de GNU, versión 2; cada archivo dice si "solo" o "o posterior". Cada parche tiene la licencia del programa que cambia. Kodi es GPL-2.0-or-later; el módulo Dolby Vision que se le añade es GPL-3.0-or-later, así que el Kodi que se compila aquí se distribuye bajo GPL-3.0-or-later.
- Los textos de las licencias están en `licenses/`, y la licencia de cada paquete, en su receta (`PKG_LICENSE`).
- Las bibliotecas de espacio de usuario y el firmware de NVIDIA son privativos y se rigen por el acuerdo de licencia del controlador de NVIDIA, no por una licencia libre. Algunos archivos de firmware de otro hardware tienen sus propias licencias.
- El código fuente completo de una build es este repositorio en el commit que nombra `BUILD_ID` en `/etc/os-release` del sistema en marcha, más los archivos de fuentes que nombran sus recetas. Mira [NOTICE.es.md](NOTICE.es.md) para los créditos, las marcas y cómo obtener el código fuente.
- El README propio de LibreELEC, con su declaración de licencia y copyright, se conserva como [README.LibreELEC.md](README.LibreELEC.md), y su guía para colaborar con LibreELEC, como [CONTRIBUTING.LibreELEC.md](CONTRIBUTING.LibreELEC.md).

## Avisar de problemas

Avisa de los problemas de Hades en el gestor de incidencias del repositorio en el que está este archivo, **no** a LibreELEC, Kodi, Dolby, NVIDIA, AMD ni Intel. Incluye, por favor:

- la build (las líneas `BUILD_ID` y `PRETTY_NAME` de `/etc/os-release`, o *Ajustes > Información del sistema*);
- la gráfica (la tarjeta, o el procesador si es su gráfica integrada), el modelo de tele y cómo están conectados;
- qué reprodujiste (contenedor, perfil Dolby Vision) y qué viste;
- el registro de Kodi, o la carpeta `hades-diag` de la unidad si el interruptor de diagnóstico estaba encendido. Repásalo antes: está filtrado, pero comprobarlo es cosa tuya.

[docs/diagnostics.md](docs/diagnostics.md) (en inglés) explica cómo sacar el diagnóstico y el registro, y qué líneas del registro dicen más.

Si el mismo problema ocurre también con una imagen oficial de LibreELEC, es de LibreELEC o de Kodi y se puede avisar allí, contra su imagen.

No hay garantía de ningún tipo. Lo usas por tu cuenta y riesgo.
