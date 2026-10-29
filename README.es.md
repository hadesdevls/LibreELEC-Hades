# LibreELEC Hades

[English](README.md) | **Español**

LibreELEC Hades es una compilación no oficial de [LibreELEC](https://libreelec.tv) para PC de 64 bits (la imagen "Generic x86_64"). Añade Dolby Vision nativo y HDR10+ por HDMI desde la gráfica del PC, sea una tarjeta gráfica o la integrada en el procesador, donde LibreELEC envía HDR10. Todo lo demás que hace LibreELEC se conserva.

> **Esto no es LibreELEC y no es oficial.** LibreELEC Hades no lo hacen, mantienen, avalan ni certifican Team LibreELEC, Team Kodi o la XBMC Foundation, Dolby Laboratories, HDR10+ Technologies, NVIDIA, AMD ni Intel, y no tiene relación con ninguno de ellos. Por favor, no les pidas ayuda sobre Hades. Kodi es una marca registrada de la XBMC Foundation. Dolby Vision y HDR10+ se nombran aquí solo como formatos de vídeo: LibreELEC Hades no usa el logotipo de ninguno de los dos y ninguno de los dos lo certifica. Las demás marcas son de sus dueños; consulta [NOTICE.es.md](NOTICE.es.md).

**Por qué la pantalla dice "LibreELEC Hades".** El centro multimedia de esta compilación es Kodi con cambios que la XBMC Foundation no ha aprobado, y la política de marca de Kodi pide a una compilación así que no use como suyos el nombre y los logos de Kodi. Por debajo es Kodi: complementos, skins y aplicaciones de mando a distancia funcionan como con Kodi, su registro sigue siendo `kodi.log` y los demás programas siguen viendo Kodi. La página "Acerca de esta compilación" de la información del sistema dice qué es y cómo apoyar a Kodi y a LibreELEC.

**Versión 1.0 RC5 (octubre de 2026).** El Dolby Vision nativo, con el perfil 7 y su capa de mejora completa, ya se ha visto funcionar en gráficas NVIDIA, AMD e Intel, con el Dolby Atmos en paso directo; lo que añade 1.0 RC5 no se ha probado todavía en hardware. Como versión candidata, necesita más hardware: si la pruebas, cuenta lo que veas en los issues. El [registro de cambios](CHANGELOG.md) dice qué cambió en cada versión.

## Qué añade a LibreELEC

- **Dolby Vision nativo por HDMI.** Cada imagen se reconstruye como la define la especificación de Dolby Vision, con la capa de mejora completa (FEL) del perfil 7, y se envía con sus metadatos en cada fotograma. La tele hace su propio procesado Dolby Vision, como con un reproductor certificado (su modo "Standard" o "TV-led"). Perfiles 5, 7 (FEL y MEL) y 8.1.
- **Gráficas NVIDIA, AMD e Intel,** tanto tarjetas como integradas (NVIDIA con sus módulos abiertos del kernel, y las tarjetas GeForce GTX 900 y GTX 10 y la TITAN V con el controlador 580 de NVIDIA).
- **Una vuelta atrás segura.** Dolby Vision solo se usa cuando el vídeo, la tele, el enlace HDMI y el controlador de la gráfica cumplen todos. Si no, el vídeo se reproduce como siempre, en HDR10 o SDR, y la página Dolby Vision de la información del reproductor dice por qué.
- **La frecuencia que Kodi elegiría,** exacta para el vídeo progresivo (25 fps a 25 Hz, 29,97 fps a 29,97 Hz). En NVIDIA se añaden los modos a 24, 30, 60 y 120 Hz exactos que el controlador no ofrecía.
- **Pensado también para gráficas integradas.** Un fotograma que ocupa casi todo un periodo de refresco se prepara un refresco antes, y la imagen bajo los menús y subtítulos se rehace solo donde están.
- **HDR10+** con los metadatos dinámicos de cada fotograma, donde el controlador de la gráfica puede enviarlos.
- **El zoom, el desplazamiento vertical y el estiramiento** conservan el Dolby Vision nativo.
- **Dolby Vision mostrado como HDR10** donde la salida nativa no es posible: siempre para el perfil 5, cuya imagen base no se puede ver por sí sola, y para el perfil 7 si lo activas. Una pantalla sin HDR recibe el HDR adaptado a SDR con el método de referencia de ITU-R BT.2390.
- **Opciones, apagadas por defecto:** Dolby Vision Low-Latency para una entrada de la tele que solo ofrece eso, adaptado a la tele por Hades; Dolby Vision con la señalización antigua de HDMI 1.4b, para las teles de 2016 que la piden; HDR10+ o HDR10 enviados como Dolby Vision, y Dolby Vision enviado como HDR10+; Dolby Vision sin sus *trims*, para proyectores.
- **Vídeo 3D de Blu-ray** (H.264 MVC): se decodifican las dos vistas, y una pantalla 3D se pone en 3D por HDMI.
- **PC con dos gráficas, y portátiles:** Kodi arranca en la tele, esté conectada a la gráfica que esté, y ofrece pasarse a una tele que se conecte después.
- **Avisos que dicen qué pasa** (una entrada de la tele que solo ofrece Dolby Vision de baja latencia, una entrada en modo HDMI 1.4, ninguna tele a la vista) y, en la información del reproductor, una página Dolby Vision y el uso y la temperatura de la gráfica.
- **Valores por defecto más seguros, un instalador para Windows y un interruptor de diagnóstico** (más abajo).

## Qué necesitas

- Un PC de 64 bits con Secure Boot desactivado (Hades todavía no arranca con él).
- Una unidad USB de 8 GB o más. Con un SSD por USB va mucho más fluido que con una memoria.
- Para Dolby Vision:
  - **Una tele que ofrezca Dolby Vision "Standard" en la entrada HDMI que uses.** Muchas teles solo ofrecen Dolby Vision de baja latencia en una entrada preparada para juegos o para un PC; Hades lo dice y envía a esa entrada HDR10, o Dolby Vision de baja latencia si lo activas. En una LG C5, "Quick Frame Transport" tiene que estar desactivado en esa entrada.
  - **Una gráfica compatible:** NVIDIA GeForce GTX 16, RTX 20 o posterior, y con el controlador 580 de NVIDIA las tarjetas GeForce GTX 900 y GTX 10 y la TITAN V; gráficas AMD Radeon con hardware de pantalla DCN 1.0 o posterior, de Raven, Picasso y Navi 1x a la serie RX 9000, y, nuevo en 1.0 RC5 y sin probar todavía en hardware, las Radeon series RX 400 y RX 500, la Radeon RX Vega M, las Radeon RX Vega 56 y 64 y la Radeon VII (hardware de pantalla DCE 11.2 y 12; no el hardware AMD más antiguo, ni las pocas versiones que nombra [docs/how-it-works.md](docs/how-it-works.md#graphics-drivers), en inglés); gráficas Intel de versión de pantalla 9 o posterior (UHD 6xx y posteriores).
  - **El PC conectado por HDMI directamente a la tele** es lo mejor. Una barra de sonido o un receptor AV en medio tiene que dejar pasar Dolby Vision.
- Tus propios vídeos o complementos: Hades no trae películas, complementos ni fuentes de contenido.

## Qué se ha probado

El autor prueba en un AMD Ryzen 9 7950X3D con una NVIDIA GeForce RTX 4080 SUPER, tanto en la tarjeta como en la gráfica integrada del procesador, con una tele LG C5, un monitor Samsung Odyssey OLED G6 (LS27DG600SLXPE), una barra de sonido TCL Q85H Pro en el eARC de la tele y un SSD USB HIKSEMI HS-USB-SYNC C PRO de 128 GB; y en un portátil con un Intel Core i5-12450H. Lo marcado "usuario" lo contaron personas que lo probaron en su propio hardware, en los issues de este repositorio y en los foros de LibreELEC y de Kodi. Visto funcionar quiere decir con colores correctos y con una vuelta limpia a los menús; el logotipo Dolby Vision de la tele, por sí solo, no cuenta. Los resultados de abajo son de 1.0 RC4 y de versiones anteriores; lo que añade 1.0 RC5 y no se ha probado todavía está en "Sin probar todavía".

### Gráficas

| Gráfica | Probada en |
| --- | --- |
| NVIDIA GeForce serie RTX 50 | RTX 5060 (autor), RTX 5080 y RTX 5090 (usuarios) |
| NVIDIA GeForce serie RTX 40 | RTX 4080 SUPER (autor), RTX 4080 Laptop GPU (usuario) |
| NVIDIA GeForce serie RTX 30 | RTX 3070 y RTX 3090 (usuarios) |
| NVIDIA GeForce series GTX 16 y RTX 20 | Sin probar todavía |
| NVIDIA GeForce series GTX 900 y GTX 10, TITAN V (controlador 580) | Sin probar todavía |
| AMD Radeon serie RX 7000 | RX 7600 (usuario) |
| AMD Radeon series RX 5000, RX 6000 y RX 9000 | Sin probar todavía |
| AMD Radeon series RX 400 y RX 500, RX Vega M, RX Vega 56 y 64, Radeon VII (nuevo en 1.0 RC5) | Sin probar todavía |
| Gráficas integradas de AMD Ryzen | Ryzen 9 7950X3D (autor), Ryzen 7 5800H y Ryzen 3200G (usuarios) |
| Gráficas integradas de Intel | Core i5-12450H (autor), Core i3-1005G1, Core 5 255H y N100 (usuarios) |
| Gráficas Intel UHD 6xx | Sin probar todavía |
| Gráficas Intel que usan el controlador `xe` (Arc serie B, Core Ultra 200V y posteriores) | Sin probar todavía |

Teles y monitores: una tele LG C5 y un monitor Samsung Odyssey OLED G6 (autor); LG G5, LG C2, una tele Panasonic y una Sony Bravia 8 II con Dolby Vision nativo (con su entrada HDMI en "Enhanced format"), una Samsung S90H, una Samsung S95B y un proyector JVC DLA-NZ9 con HDR10+, una LG C3 con Dolby Atmos en paso directo y una tele Sony que solo ofrece Dolby Vision de baja latencia (usuarios).

### Funciones

| Qué | Visto funcionar en |
| --- | --- |
| Dolby Vision perfil 7 con FEL | RTX 4080 SUPER, Ryzen 9 7950X3D y Core i5-12450H (autor); RTX 4080 Laptop GPU, RTX 5080, RX 7600, Ryzen 7 5800H y Core 5 255H (usuarios) |
| Dolby Vision perfil 7 con MEL | Ryzen 9 7950X3D (autor); RX 7600 (usuario) |
| Dolby Vision perfiles 8.1 y 5 | RTX 4080 SUPER y Ryzen 9 7950X3D (autor); RX 7600 (usuario) |
| Dolby Vision en dos pistas de vídeo | `.m2ts` en la RTX 4080 SUPER (autor); MKV en una RTX 3070, y MP4 en un portátil con gráfica RTX (usuarios) |
| Menús y subtítulos sobre Dolby Vision sin perder fotogramas | Perfiles 7 FEL y 8.1 en la RTX 4080 SUPER y el Ryzen 9 7950X3D (autor); perfil 7 FEL en una RX 7600 (usuario, medido con los menús sobre la imagen: 240 de 240 imágenes, el mayor hueco de 42,0 ms, issue #3) |
| Dolby Vision Low-Latency | Ryzen 9 7950X3D, a través de una caja de sincronización HDMI que solo ofrece Dolby Vision de baja latencia (autor) |
| Dolby Vision mostrado como HDR10 | Perfil 5 en la RTX 4080 SUPER (autor); perfiles 5 y 7 FEL en un Core i3-1005G1 (usuario) |
| Dolby Vision mostrado como HDR10+ | Perfil 7 FEL en la RTX 4080 SUPER con un Samsung Odyssey OLED G6 (autor); perfil 7 FEL en una RX 7600 con un JVC DLA-NZ9, mostrado como HDR10+ con su FEL a resolución completa (usuario, issue #10); un Core i3-1005G1 con una Samsung S90H (usuario, issue #2) |
| HDR10+ | RTX 4080 SUPER con un Samsung Odyssey OLED G6 (autor); Core i3-1005G1 con una Samsung S90H, Ryzen 3200G con una Samsung S95B, Ryzen 7 5800H, y RX 7600 con un JVC DLA-NZ9 (usuarios; en la RX 7600, medido: los paquetes HDR10+ de cuatro películas HDR10+, byte a byte los de un reproductor con licencia en 10 de 10 muestras, issue #10) |
| HDR10+ como Dolby Vision | Ryzen 7 5800H (usuario) |
| HDR10 a más de 8 bits en NVIDIA | RTX 4080 SUPER (autor) |
| Un vídeo a 24 fps a 24 Hz exactos en NVIDIA | RTX 4080 SUPER (autor) |
| Dolby TrueHD con Atmos, en paso directo | RTX 4080 SUPER, Ryzen 9 7950X3D y Core i5-12450H (autor); RTX 3090 (usuario) |
| Dolby Digital Plus con Atmos y DTS-HD Master Audio, en paso directo | RTX 4080 SUPER (autor) |
| 3D: las dos vistas decodificadas y la pantalla puesta en 3D | Ryzen 7 5800H, con una pantalla que no se nombró (usuario) |
| El uso y la temperatura de la gráfica en la información del reproductor | Un portátil con gráfica RTX (usuario) |
| Kodi en la tele en un PC con dos gráficas | El PC del autor, con la tele en la tarjeta NVIDIA |
| Arrancado desde USB con los discos internos ocultos | El PC del autor |
| Instalador para Windows | "Actualizar" en una unidad USB real (autor); todas las demás operaciones, solo en un disco virtual |

Sin probar todavía:

- Dolby Vision en vídeo AV1.
- HDR10 enviado como Dolby Vision, y Dolby Vision sin sus *trims*.
- HDR, HLG y Dolby Vision adaptados a SDR en una pantalla sin HDR.
- Dolby Vision nativo en una tele con un bloque Dolby Vision antiguo (versión 1) o con gestión de pantalla 2 o 3.
- Dolby Vision a través de una barra de sonido o un receptor AV.
- 3D desde un disco Blu-ray, su imagen o su carpeta.
- Una tele conectada después de arrancar Kodi, los modos nuevos de una tele tomados mientras Kodi funciona (al activar Deep Colour en la tele, por ejemplo), y llevar Kodi a otra pantalla con el ajuste Monitor de *Sistema > Pantalla*.
- Hades instalado en un disco interno de un PC real (solo en una máquina virtual).
- Lo nuevo de 1.0 RC4:
  - las tarjetas GeForce GTX 900 y GTX 10 y la TITAN V con el controlador 580, y las gráficas Intel que usan el controlador `xe`;
  - el zoom, el desplazamiento vertical y el estiramiento con Dolby Vision nativo;
  - el perfil 8.4 (capa base HLG, como graban los iPhone);
  - en NVIDIA, HDR10 a una tele que declara 3D por HDMI, y HDR10 a 3840x2160 a 24, 25 y 30 Hz a una tele que toma esos modos por su VIC de HDMI;
  - el SDR a 8 bits por HDMI en NVIDIA e Intel, y "Profundidad de la salida HDR (NVIDIA)" en 8 bits;
  - los menús Java de Blu-ray (BD-J) con el complemento "JRE for BD-J menus";
  - la salida de audio de la pantalla que usa Kodi, elegida por él mismo;
  - "Dolby Vision medido de la imagen HDR10" para un vídeo;
  - Dolby Vision con la señalización antigua de HDMI 1.4b, en una tele que la pide (las de 2016, entre ellas las LG OLED de ese año), y el Low-Latency en un conector DisplayPort de doble modo de NVIDIA;
  - *Discos internos que se muestran* y *Particiones internas que se muestran*, en los ajustes de LibreELEC;
  - los demás añadidos de 1.0 RC4 que lista el registro de cambios.
- Lo nuevo de 1.0 RC5:
  - Dolby Vision enviado en IPT, el valor por defecto para los perfiles 7 y 8;
  - la señal Dolby Vision puesta con el cambio de modo, antes de la primera imagen;
  - Dolby Vision para los menús, la interfaz sobre HDR convertida exactamente, y la caché de la interfaz sobre Dolby Vision nativo;
  - los saltos que ya no detienen el dibujo, y un flujo que se queda sin datos un momento y conserva su grupo de imágenes y su capa FEL;
  - el Dolby Vision nativo en sincronía con el sonido como la salida normal;
  - la entrada de arranque «Installer», que ahora da al sistema instalado los menús de arranque y el GRUB de Hades, arranca también un disco MBR en UEFI y comprueba cada paso de la instalación;
  - el Dolby Vision nativo en las gráficas AMD con hardware de pantalla DCE 11.2 y 12 (Radeon series RX 400 y RX 500, RX Vega M, RX Vega 56 y 64, Radeon VII), y por un HDMI que NVIDIA saca por una salida DisplayPort;
  - Dolby Vision enviado como HDR10+ en Intel a 2160p24, 25 y 30, y HDR10+ en las RTX 50, junto a la frecuencia variable por HDMI en las GTX 16 a RTX 30, y a pantallas de versión de aplicación 0;
  - los MP4 de doble pista cuya pista de mejora es una pista HEVC corriente, y los subtítulos 3D a la profundidad del disco;
  - los demás añadidos de 1.0 RC5 que lista el registro de cambios.

SDR, HDR10, HLG, el paso directo de audio, los menús y el cambio de frecuencia usan el código de Kodi y se comportan como en LibreELEC, salvo por lo añadido arriba.

## Límites conocidos

- **Dolby Vision necesita una señal HDMI intacta** desde un puerto HDMI de la gráfica (en NVIDIA, también desde un puerto DisplayPort que la gráfica maneja como HDMI, como el puerto HDMI de algunos portátiles). La frecuencia variable, el escalado y los ajustes de color del controlador de la gráfica no son compatibles con él, y un conversor entre DisplayPort o USB-C y HDMI no lo transmite.
- El Dolby Vision nativo sale a 3840x2160, hasta 60 Hz. En las gráficas Intel UHD 6xx, hasta 30 Hz y solo desde un puerto HDMI de la propia gráfica.
- En las gráficas integradas de AMD, el primer vídeo Dolby Vision tras instalar una compilación nueva puede dar tirones breves mientras Hades mide cuál de dos formas de dibujar es más rápida ahí.
- En gráficas integradas, el perfil 5 con la información del reproductor abierta perdía fotogramas en las pruebas. Lo que cambian 1.0 RC3 y 1.0 RC4 para eso está sin probar todavía.
- **Las tarjetas GeForce GTX 900 y GTX 10 y la TITAN V** usan el controlador 580 de NVIDIA, que Hades elige al arrancar según la tarjeta. Un PC con una de ellas y a la vez una tarjeta NVIDIA más nueva recibe el controlador más nuevo, y la tarjeta antigua se queda sin ninguno. En las GTX 900 y GTX 10, el HDR10 sale a 8 bits por HDMI: el controlador 580 no tiene para ellas una salida HDMI de más bits.
- **Dolby Vision Low-Latency lo adapta Hades, no la gestión de pantalla de Dolby**, que no es pública. La tele está en su modo Dolby Vision y muestra la adaptación de Hades al brillo y los colores que da su bloque Dolby Vision.
- **HDR10+ solo sale donde el controlador de la gráfica puede enviarlo con cada fotograma;** si no, sale como HDR10. [docs/how-it-works.md](docs/how-it-works.md#hdr10) (en inglés) dice dónde. La información del reproductor dice "HDR10+" mientras salen sus metadatos.
- **HDR10 enviado como Dolby Vision** lleva metadatos medidos en la imagen mientras se reproduce, no los del estudio: los cortes de escena se detectan en la imagen, y alguno se escapa o se detecta donde no lo hay. **HDR10+ enviado como Dolby Vision** conserva el brillo de cada escena, no la curva de tonos del estudio, su pantalla de destino ni sus ventanas.
- **El 3D es nuevo.** El autor no lo ha visto en una pantalla 3D, y no se ha probado ningún disco 3D. Si un vídeo 3D sale en negro o roto, desactiva *Sistema > Pantalla > Poner la pantalla en 3D (HDMI)*. Más en [docs/how-it-works.md](docs/how-it-works.md#stereoscopic-3d) (en inglés).
- Sin Secure Boot.
- Es una implementación independiente, escrita a partir de especificaciones públicas. Ni Dolby ni HDR10+ Technologies la han probado ni certificado, y puede diferir de un reproductor certificado.

## Valores por defecto distintos de los de LibreELEC

No se quita nada de LibreELEC. Lo que es inseguro por defecto en un PC que también guarda tus otros discos se deja con un valor seguro y se puede volver a activar.

- **Los discos de tu PC.** Arrancado desde USB (las entradas "Run" y "Live"), los discos internos no existen para Hades: no se inicia ni se conserva ningún controlador de almacenamiento interno, sea del tipo que sea, hasta el siguiente arranque ([cómo](docs/settings.md#outside-the-settings-screens)). La entrada "Run (internal disks read-only)" los muestra en solo lectura. Instalado en un disco interno, Hades tampoco ve los demás discos NVMe y SATA del PC, ni monta las demás particiones de su propio disco. Las unidades USB y las tarjetas SD se montan como siempre.
- **Mostrar los discos internos.** En los ajustes de LibreELEC, *Sistema > Discos internos* puede hacer que la entrada de solo lectura sea la que arranca por defecto. Está apagado, y antes de encenderlo explica qué significa: los discos a la vista los puede leer todo lo que se ejecuta en Kodi, complementos incluidos; "solo lectura" es cómo se montan, no un candado; y esa entrada arranca el kernel sin su bloqueo. Elegir "Run" una vez en el menú de arranque, restablecer los valores por defecto o actualizar con el instalador lo vuelve a apagar. Debajo, *Discos internos que se muestran* aparta del kernel los discos NVMe y SATA que desmarques (el de Windows, un disco de archivos personales): en cada arranque que muestra los discos internos se sueltan antes de que se ejecute nada más, y el kernel no conecta ningún otro disco interno hasta el siguiente arranque y funciona con su bloqueo, así que ni un programa con todos los permisos llega a ellos. Los discos se distinguen por su modelo y su número de serie; uno que no se puede distinguir también se aparta mientras haya alguno apartado, y todos si no se puede leer la lista. Debajo de eso, *Particiones internas que se muestran* hace que las particiones que desmarques no se monten, por el número de serie de su disco; una partición que no se monta sigue en su disco, donde un programa así podría leerla.
- **Bloqueo del kernel.** Las entradas que ocultan los discos arrancan el kernel en modo de bloqueo "integrity", y un arranque que aparta discos internos lo bloquea antes de que arranque el sistema: solo carga los módulos firmados para esa compilación.
- **La entrada del instalador** del menú de arranque está, pero Hades arranca en "Run".
- **Samba (compartir archivos) está apagado.** Al encenderlo, pide una contraseña que Hades genera y muestra en los ajustes; se puede seguir eligiendo compartir sin contraseña.
- **SSH está apagado,** como en LibreELEC, y mientras la contraseña de root sea la de fábrica solo acepta claves.
- **El cortafuegos** arranca en modo "home" en una instalación nueva, y **AirPlay** está apagado.
- **Actualizaciones.** Hades no contacta con el servidor de actualizaciones de LibreELEC, porque una actualización oficial de LibreELEC quitaría Dolby Vision. Se actualiza a mano; *Actualizaciones > Versiones disponibles* dice dónde se publican. Los archivos de actualización que no son de Hades se rechazan. Por defecto no se envían estadísticas de uso.
- **Bluetooth** usa reglas de emparejamiento más estrictas, con una opción para dispositivos de entrada antiguos.
- **Reloj.** El reloj de la placa se deja en hora local y nunca se escribe, así que Windows mantiene la hora correcta. En un PC sin Windows, o cuyo Windows guarda el reloj en UTC, *Sistema > Identificación > Reloj de la placa* lo pasa a UTC desde el siguiente arranque.
- **Endurecimiento.** Ajustes del kernel y de sysctl que dificultan los ataques (un archivo en `/storage/.config/sysctl.d` puede devolver los de sysctl a como estaban), BusyBox sin setuid root, y los datos de los complementos, las contraseñas de red y demás secretos legibles solo por root. Los programas de 32 bits están apagados (`ia32_emulation=1` en la línea del kernel los activa).
- **TRIM del SSD.** Una vez a la semana, con la prioridad más baja, los bloques sin usar de la partición de almacenamiento se devuelven a la unidad. `systemctl mask fstrim.timer` lo apaga.
- **Rango RGB en AMD.** Las gráficas AMD envían RGB en rango limitado a una tele que no puede elegir el rango, como ella espera. Si activaste el ajuste de Kodi de rango de color limitado (16-235), en *Sistema > Pantalla*, para una tele así, desactívalo.
- **Mapeo de tonos.** En una pantalla sin HDR, el método de mapeo de tonos de Kodi es por defecto "Referencia (BT.2390)". Una elección guardada de Hable, el método por defecto de Kodi, se cambia a ese una vez.

## Qué no incluye

- **Ningún complemento de terceros ni fuente de contenido.** Como LibreELEC, la imagen trae los complementos estándar de Kodi, los repositorios de complementos de Kodi y de LibreELEC, y las carpetas locales vacías de LibreELEC. Lo que instales es cosa tuya.
- **Ningún software de Dolby.** Hades no tiene código, biblioteca ni clave de licencia de Dolby, ni código o especificación de HDR10+ Technologies (las estructuras del InfoFrame de HDR10+ y del InfoFrame del fabricante de Dolby salen de código GPL; mira "Créditos").
- **Ninguna telemetría.** Hades no añade ninguna y, por defecto, no envía estadísticas de uso ni ningún identificador de tu PC. Como LibreELEC y Kodi, contacta con los servicios que ellos usan (repositorios de complementos, servidores de hora y similares), que lista la página "Política de privacidad" de la información del sistema. El diagnóstico se queda en tu unidad.

## Instalar y arrancar

Necesitas el archivo de imagen (`LibreELEC-Generic.x86_64-...img.gz`), de las [publicaciones](https://github.com/hadesdevls/LibreELEC-Hades/releases) de este repositorio o compilado por ti.

1. Escríbelo en una unidad USB con el instalador para Windows (`LibreELEC-Hades-Installer.exe`, que trae su propio LEEME) o con cualquier programa que escriba imágenes de disco. La unidad se borra.
2. Reinicia el PC, abre el menú de arranque del firmware (F8, F11, F12 o Esc en la mayoría de los PC) y elige la unidad USB.
3. El primer arranque prepara el almacenamiento y reinicia una vez.

Para volver a tu sistema de siempre, apaga Hades y arranca el PC con normalidad. Arrancado desde USB, Hades no cambia nada en el arranque del PC ni en Windows.

## Actualizar

- **Con el instalador para Windows:** elige la unidad que tiene Hades y "Actualizar". Se conservan los ajustes, los complementos y la carpeta del diagnóstico.
- **Como en LibreELEC:** copia el archivo `.tar` de la versión nueva en la carpeta `Update` (`/storage/.update`) y reinicia. Por la red, esa carpeta es un recurso compartido de Samba, y Samba está apagado en Hades hasta que lo enciendas.

## Compilar desde el código fuente

A grandes rasgos; para todo lo demás vale la documentación de LibreELEC.

1. Usa una máquina Linux (el autor usa Ubuntu 26.04; LibreELEC mantiene recetas de contenedor en `tools/docker/`), un usuario sin privilegios, una ruta sin espacios y unos 75 GB libres en disco.
2. Descarga este repositorio: `main` es la versión actual, y el código fuente de una compilación es el commit que nombra su `BUILD_ID`.
3. Compila: `PROJECT=Generic DEVICE=Generic ARCH=x86_64 make image`
4. Los resultados quedan en `target/`: un `.img.gz` (para una unidad USB), un `.tar` (para actualizar) y un `.ova` (máquina virtual).

Conviene saber:

- Cada fuente se descarga de la dirección de su receta (`package.mk`) y se comprueba con el SHA-256 que hay escrito en ella. Ninguna receta tiene el hash vacío, y no se usan los paquetes de herramientas precompilados "reusable" (`USE_REUSABLE="no"`).
- Los paquetes del controlador de NVIDIA (615 y, para las tarjetas GeForce GTX 900 y GTX 10 y la TITAN V, 580) se descargan de NVIDIA y se desempaquetan sin ejecutarlos. Solo se usan, sin cambios, sus bibliotecas de espacio de usuario y su firmware; los módulos del kernel de 615 y la biblioteca GBM de su EGL (egl-gbm) se compilan desde el código abierto de NVIDIA. El módulo del kernel de 580 se compila como lo entrega NVIDIA, con su núcleo cerrado (`nv-kernel.o_binary`), y su núcleo de modos de pantalla y `nvidia-drm` se compilan desde el código abierto de NVIDIA de la misma versión. Al compilar, aceptas la licencia de NVIDIA para esos archivos. El controlador 580 hace la imagen unos 120 MB más grande.
- Una compilación limpia tarda de 20 a 45 minutos en una máquina de 32 hilos, mucho más en una pequeña. Las compilaciones del autor se hacen sin acceso a la red, con las fuentes descargadas y comprobadas antes.
- **Si cambias el árbol y compartes tu compilación, dale un nombre propio** (`BUILDER_NAME=<tu nombre> make image`) y di que es tuya. Sin él, la compilación se llama a sí misma "Hades 1.0 RC4".
- El instalador para Windows es un programa pequeño aparte, en C#, en `tools/hades-installer/`; `build.ps1` lo compila con el compilador que ya trae Windows.

## Documentación

En inglés:

- [Cómo funciona la salida Dolby Vision](docs/how-it-works.md): del archivo al cable HDMI, cada controlador de gráfica, HDR10+ y 3D.
- [Ajustes](docs/settings.md): todos los ajustes de esta compilación.
- [Diagnóstico y registros](docs/diagnostics.md): cómo obtenerlos y qué mirar.

Versiones en 1.0 RC5: Linux 7.3, Kodi 22 (la rama `Piers` posterior a 22.0rc1, hasta 7f485d6bcbc4, y nueve commits del `master` de Kodi), FFmpeg 9.0.2, Mesa 26.2.6, libplacebo 7.387.0 (275f7dea2792), NVIDIA 615.78.08 y, para las tarjetas GeForce GTX 900 y GTX 10 y la TITAN V, NVIDIA 580.196.07, sobre LibreELEC `master` (desarrollo de 13.0, hasta 0314a6cb55d0). LibreELEC no ha publicado la versión 13.

## Créditos

Lo que esta compilación añade a LibreELEC se escribió para ella, salvo lo que se nombra aquí, que es trabajo de otras personas y se usa con agradecimiento:

- **El fork de LibreELEC con Dolby Vision de CroqueMr, y quienes han contribuido a él** ([CroqueMr/libreelec-x86-DV](https://github.com/CroqueMr/libreelec-x86-DV)), donde empezó este trabajo: su módulo de Dolby Vision para Kodi (`dvbridge`), del que creció la salida Dolby Vision de esta compilación, su salida Dolby Vision para gráficas Intel en el kernel de Linux (el InfoFrame de Dolby que sus parches añadían a `drivers/video/hdmi.c`, con la misma interfaz, se escribió de nuevo para esta compilación, bajo la licencia de ese archivo) y sus cambios en FFmpeg y libplacebo, entre ellos un escalador más rápido para el perfil 7. Algunas funciones siguen ideas de sus versiones posteriores y se escribieron de nuevo aquí.
- **Dom Cobley (popcornmix)**: la decodificación del vídeo 3D de Blu-ray (sus cambios en el decodificador H.264 de FFmpeg) y cómo lee Kodi los discos 3D.
- **cinemaONE** ([cinema-ONE/LibreELEC.tv](https://github.com/cinema-ONE/LibreELEC.tv)): los cambios en el controlador del kernel de AMD para el 3D por HDMI, y la *pull request* 29463 de Kodi (menús opacos sobre vídeo HDR).
- **Adrian Betschart**: el cambio en el controlador del kernel de AMD que envía el RGB por HDMI en rango limitado en los modos CTA, que forma parte de Linux 7.3 y en el que se apoyan dos parches de esta compilación.
- **quietvoid** ([dovi_tool](https://github.com/quietvoid/dovi_tool) y [hdr10plus_tool](https://github.com/quietvoid/hdr10plus_tool), MIT): referencias para los metadatos de Dolby Vision y de HDR10+.
- **CoreELEC** y el código GPL del kernel de Amlogic que lleva, leído como dato: las estructuras del InfoFrame de HDR10+ y de los InfoFrames del fabricante de Dolby, la señal de Low-Latency, y qué InfoFrame se envía a cada pantalla Dolby Vision.
- **YBLOD** ([dangerouslaser/libreelec-yblod](https://github.com/dangerouslaser/libreelec-yblod)): ideas para algunas funciones, escritas de nuevo aquí.
- **Arreglos de otros proyectos, tomados como parches con sus autores:** entre ellos, en esta versión, avahi, expat, OpenSSL, glibc, FFmpeg, BlueZ, BusyBox, libdvdcss (el cambio de Saifelden Mohamed Ismail para los discos CPXM), los commits de la rama `Piers` de Kodi, y el arreglo de User65k al UPnP de Kodi, tomado del Kodi de CoreELEC.
- **Team LibreELEC y Team Kodi**, y los autores de cada programa de la imagen. Los parches tomados de otros proyectos conservan a sus autores en su cabecera (los dieciséis archivos del fork de CroqueMr que no la tienen se nombran en [NOTICE.es.md](NOTICE.es.md)), y el commit de OpenSSL 4 de Rudi Heitbaum, de LibreELEC, conserva a su autor en el historial de este repositorio.

[NOTICE.es.md](NOTICE.es.md) los lista con sus licencias.

## Licencias y código fuente

- El sistema de compilación de LibreELEC está bajo la Licencia Pública General de GNU, versión 2; cada archivo dice si "solo" o "o posterior". Cada parche tiene la licencia del programa que cambia. Kodi es GPL-2.0-or-later. Tal como lo compila LibreELEC, va enlazado con el cliente de Samba (GPL-3.0-or-later), FFmpeg compilado bajo la versión 3 de la GPL y OpenSSL (Apache-2.0), así que el Kodi compilado aquí se distribuye bajo GPL-3.0-or-later. El módulo de Dolby Vision que se le añade es GPL-3.0-or-later, y lo que ha escrito este proyecto en él se ofrece también bajo GPL-2.0-or-later ([NOTICE](NOTICE.es.md)).
- Los textos de las licencias están en `licenses/`, y la licencia de cada paquete está en su receta (`PKG_LICENSE`).
- Las bibliotecas de espacio de usuario y el firmware de NVIDIA, y el núcleo cerrado del módulo del kernel del controlador 580 (que solo se carga para las tarjetas GeForce GTX 900 y GTX 10 y la TITAN V), son privativos y se rigen por el NVIDIA Driver License Agreement, no por una licencia libre. Algunos archivos de firmware de otro hardware tienen sus propias licencias.
- El código fuente completo de una compilación es este repositorio en el commit que nombra `BUILD_ID` en `/etc/os-release` del sistema en marcha, más los archivos de fuentes que nombran sus recetas. Mira [NOTICE.es.md](NOTICE.es.md) para los créditos, las marcas y cómo obtener el código fuente.
- El README de LibreELEC, con su licencia y su aviso de copyright, se conserva como [README.LibreELEC.md](README.LibreELEC.md), y su guía para contribuir a LibreELEC como [CONTRIBUTING.LibreELEC.md](CONTRIBUTING.LibreELEC.md).

## Avisar de problemas

Avisa de los problemas de Hades en los [issues](https://github.com/hadesdevls/LibreELEC-Hades/issues) de este repositorio, no a LibreELEC, Kodi, Dolby, HDR10+ Technologies, NVIDIA, AMD ni Intel. Puedes escribir en inglés o en español. Incluye, por favor:

- la compilación (*Ajustes > Información del sistema*, o las líneas `PRETTY_NAME` y `BUILD_ID` de `/etc/os-release`);
- la gráfica (la tarjeta, o el procesador si es la integrada), el modelo de la tele y cómo están conectadas;
- qué reprodujiste (contenedor, perfil Dolby Vision o formato HDR) y qué viste;
- el registro de Kodi, o la carpeta `hades-diag` si el interruptor de diagnóstico estaba encendido. Revísala antes: está filtrada, pero es tuya y te toca comprobarla.

[docs/diagnostics.md](docs/diagnostics.md) (en inglés) dice cómo obtenerlos, también desde un sistema instalado en un disco interno.

Si el mismo problema pasa con una imagen oficial de LibreELEC, es de LibreELEC o de Kodi y se puede avisar allí.

No hay garantía de ningún tipo. Lo usas bajo tu propia responsabilidad.
