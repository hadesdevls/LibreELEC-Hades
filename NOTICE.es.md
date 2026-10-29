# AVISO sobre LibreELEC Hades

(In English: NOTICE.md)

## Qué es

LibreELEC Hades es una compilación no oficial de LibreELEC para PC de 64 bits (Generic x86_64), con cambios. Lo principal que añade es la salida nativa de Dolby Vision por HDMI desde Kodi en un PC, con una tarjeta gráfica o con la gráfica integrada en el procesador. README.es.md, en el repositorio del proyecto, dice qué hace y dónde se ha probado.

**No es LibreELEC y no es oficial.** LibreELEC Hades no está hecho, mantenido, avalado ni certificado por Team LibreELEC, por Team Kodi ni la XBMC Foundation, por Dolby Laboratories, ni por NVIDIA, AMD o Intel, y no tiene relación con ninguno de ellos. No les pidas ayuda con él. El centro multimedia de esta compilación es una versión modificada de Kodi; no lo suministra ni lo avala la XBMC Foundation, ni es un producto suyo, y no se presenta como Kodi: su pantalla dice "LibreELEC Hades", y una página de su información del sistema dice qué es.

La salida de Dolby Vision es una implementación independiente que sigue una especificación pública (ETSI GS CCM 001). No contiene código de Dolby, y Dolby no la ha probado ni certificado.

HDR10+ se nombra en esta compilación solo como formato: los metadatos que lee de un vídeo y envía a una pantalla. LibreELEC Hades no está certificado por HDR10+ Technologies.

## Trabajo de otros sobre el que se apoya

- **LibreELEC** (Team LibreELEC, y antes OpenELEC): la distribución entera y su sistema de compilación. El código original de LibreELEC está bajo la GNU General Public License, versión 2. https://github.com/LibreELEC/LibreELEC.tv
- **Kodi** (Team Kodi y sus colaboradores): el centro multimedia. GPL-2.0-or-later. https://github.com/xbmc/xbmc
- **El fork de LibreELEC con Dolby Vision de CroqueMr**, y quienes colaboran en él: de ahí parte este trabajo. Se usan, tal como los publicó o adaptados, su módulo de Dolby Vision para Kodi (`dvbridge`, GPL-3.0-or-later), su salida de Dolby Vision para gráficas Intel en el kernel de Linux y sus cambios en FFmpeg y libplacebo. El InfoFrame de Dolby que sus parches del kernel añadían a `drivers/video/hdmi.c` y `include/linux/hdmi.h` se escribió de nuevo para esta compilación, con la misma interfaz (`linux-9899`), y se ofrece bajo el aviso de permiso de esos dos archivos (Copyright (C) 2012 Avionic Design GmbH; la licencia MIT). https://github.com/CroqueMr/libreelec-x86-DV
- **FFmpeg**, **libplacebo** y el **kernel de Linux**, con parches.
- **Dom Cobley (popcornmix)**: la decodificación de la segunda vista del vídeo H.264 MVC (Blu-ray 3D), su serie de cambios al decodificador H.264 de FFmpeg (pull request 24303 de FFmpeg, LGPL-2.1-or-later), aplicada aquí a una copia de ese decodificador; y, en Kodi (GPL-2.0-or-later), cómo se leen a la vez los dos clips de un disco 3D y un cambio en cómo se leen los modos 3D de una pantalla, tal como él los escribió.
- **El árbol de LibreELEC de cinema-ONE**: cuatro cambios al controlador de gráficas AMD del kernel de Linux para el 3D por HDMI, usados aquí con arreglos. https://github.com/cinema-ONE/LibreELEC.tv
- **Adrian Betschart**: el cambio al controlador del kernel de AMD que envía el RGB por HDMI en rango limitado en los modos CTA, que forma parte de Linux 7.3; dos parches de esta compilación se apoyan en él (`linux-9971`, `linux-9983`).
- **cinemaONE**: la pull request 29463 de Kodi (GPL-2.0-or-later), sus dos cambios tal como los escribió: las partes opacas de la interfaz siguen opacas cuando se compone sobre un vídeo HDR, y el vídeo se ve entonces a través de una ventana de vídeo.
- **Los módulos de kernel abiertos de NVIDIA** (NVIDIA Corporation, MIT/GPL-2.0), compilados desde su código publicado con parches de este proyecto, **egl-gbm** (NVIDIA Corporation, MIT), la biblioteca de la plataforma GBM del EGL de NVIDIA, compilada desde su código publicado en lugar de la copia del instalador de NVIDIA, y **nvidia-vaapi-driver** (MIT); para las tarjetas GeForce GTX 900 y 10 y TITAN V, las partes abiertas del controlador 580 de NVIDIA (NVKMS y nvidia-drm, MIT/GPL-2.0) se compilan igual, con parches de este proyecto.
- **Jost**, una tipografía de indestructible type* (SIL Open Font License 1.1): con ella está escrito el nombre de la compilación en sus imágenes, que dibuja un guion de este proyecto (`packages/mediacenter/kodi/brand`).
- **avahi** (sus autores, LGPL-2.1-or-later): 42 cambios de su propio repositorio, para 17 CVE y otros 10 fallos, aplicados a avahi 0.8 como parches que conservan a sus autores. `avahi-9043`, que limita los clientes de su servidor del protocolo simple (CVE-2025-59529), se escribió para esta compilación a partir de las ideas del aviso de avahi, de su pull request 808 y de la revisión de esa pull request.
- **libdvdcss** (VideoLAN y quienes contribuyen a él, GPL-2.0-or-later): el cambio `811ce97` de Saifelden Mohamed Ismail, que lee los discos CPXM que no tienen preparado el descifrado, aplicado a libdvdcss 1.6.0 en la compilación de Kodi.
- **libbluray** (VideoLAN y sus colaboradores, LGPL-2.1-or-later): la parte Java de su soporte de los menús Java de los Blu-ray (BD-J), `/usr/share/java/libbluray-j2se-<versión>.jar` y `libbluray-awt-j2se-<versión>.jar`, se compila para esta imagen con las mismas fuentes y parches que la biblioteca (`packages/multimedia/libbluray-bdj`). Contiene **ASM** 5.0.4, Copyright (c) 2000-2011 INRIA, France Telecom, con la licencia BSD de 3 cláusulas (su texto en la imagen: `/usr/share/licenses/libbluray-bdj/ASM-LICENSE.txt`). La imagen no trae ningún entorno de ejecución de Java: los menús usan el que instala el usuario.
- **dovi_tool** y **hdr10plus_tool**, de quietvoid (MIT): referencias para la sintaxis de los metadatos de Dolby Vision y de HDR10+, y origen de reglas y valores por defecto que esta compilación sigue cuando hace metadatos de Dolby Vision (los de dovi_tool para el content mapping v2.9, las escenas de hdr10plus_tool).
- **CoreELEC**, y el código de **Amlogic** que lleva (GPL-2.0), leído como dato: la disposición del InfoFrame de fabricante de HDR10+ y la codificación de sus campos, la disposición de bytes del InfoFrame de fabricante de Dolby Vision (`linux-9899`, y el de HDMI 1.4b, `linux-9993`), la señal de Low-Latency, la parte de mapeo de contenido v2.9 que se envía a las pantallas de gestión de pantalla 2 y 3, y qué señal recibe cada pantalla Dolby Vision, entre ellas las de bloques de versión 1; no se copia código suyo. Y un arreglo del analizador HTTP del UPnP de Kodi de User65k, tomado del Kodi de CoreELEC (`kodi-9012`). https://github.com/CoreELEC
- **OpenSSL 4**: el cambio a la receta de LibreELEC es de Rudi Heitbaum (OpenSSL 4.0.3, pull request 11104 de LibreELEC), tomado como su commit y actualizado aquí desde entonces.
- **Estuary**, la piel de Kodi (phil65, Team Kodi; su código GPL-2.0, sus imágenes CC BY-SA 4.0): la información del reproductor de esta compilación se hace de sus archivos para todas las pieles (`system/hades`), con cuatro de sus imágenes.
- **YBLOD** (dangerouslaser/libreelec-yblod): ideas para algunas funciones, escritas de nuevo aquí; no se usa código suyo.
- Arreglos de los repositorios de cada proyecto (entre ellos FFmpeg, glibc, OpenSSL, curl, avahi, expat, BlueZ, libbluray, libarchive, libtiff, libass, BusyBox, OpenSSH, libnfs y la rama `Piers` de Kodi, y los de Debian para tinyxml), aplicados como parches con sus autores.
- Cambios de este proyecto a otros paquetes de LibreELEC (valores por defecto, guiones y recetas), cada uno en el árbol con su motivo.
- Todo lo demás viene de LibreELEC sin cambios, cada paquete con su licencia.

Cada parche de esta compilación dice en su cabecera de dónde viene, qué hace y por qué, salvo dieciséis archivos del fork de CroqueMr, que se conservan como el fork los publicó, sin cabecera: `linux-9900-00` a `linux-9900-07` y `linux-9901` (gráficas Intel), `ffmpeg-9900-00` a `ffmpeg-9900-02`, `libplacebo-9900-0`, `libplacebo-9910-2`, `libplacebo-9910-6-centered-lanczos` y `kodi-9990-native-dv` (con `tools/dvbridge/PROVENANCE.json`). Sus cambios son del fork, con la licencia de los archivos que cambian.

## Licencias

- Los textos de las licencias están en la carpeta `licenses`, junto a este archivo.
- Kodi es GPL-2.0-or-later. El programa Kodi de esta compilación va enlazado con el cliente de Samba (GPL-3.0-or-later), FFmpeg compilado bajo la versión 3 de la GPL y OpenSSL (Apache-2.0), tal como lo compila LibreELEC, así que se distribuye bajo GPL-3.0-or-later. El módulo de Dolby Vision que se le añade es GPL-3.0-or-later.
- El código que ha escrito este proyecto se ofrece también bajo GPL-2.0-or-later, a elección de quien lo use: sus propios archivos (los que tienen a Hades en su línea de copyright) y sus cambios a otros archivos (los parches de esta compilación cuyo autor es Hades). El código de otros autores conserva sus propios términos, también dentro del módulo de Dolby Vision.
- La licencia de cada paquete figura en su receta (`PKG_LICENSE` en su `package.mk`).

## Partes que no son software libre

Como las imágenes de LibreELEC, esta contiene firmware para muchos dispositivos y microcódigo de procesadores Intel, con sus propias condiciones. Del firmware y el microcódigo que van dentro del kernel, los textos de esas condiciones están en la carpeta `licenses` (`LicenseRef-LICENSE.*`, `LicenseRef-LICENCE.*` y `LicenseRef-intel-ucode.txt`). De todo el firmware que viene de linux-firmware, el sistema en marcha tiene su `WHENCE` (bajo qué licencia va cada archivo, con los avisos de copyright) y todos los textos de licencia, en `/usr/share/licenses/kernel-firmware`. Contiene además:

- **Bibliotecas de espacio de usuario y firmware de NVIDIA**, copyright de NVIDIA Corporation, tomados sin modificar del instalador del controlador de NVIDIA y solo para usarse con tarjetas gráficas NVIDIA. Se rigen por el **NVIDIA Driver License Agreement**, cuyo texto es `licenses/LicenseRef-NVIDIA-Driver-License-Agreement.txt`. No hay código fuente disponible de ellos.
- **El controlador 580 de NVIDIA para GeForce GTX 900 y 10 y TITAN V** (Maxwell, Pascal y Volta: los 160 identificadores PCI que la lista de NVIDIA envía a su rama 580), que la imagen elige al arrancar por el identificador PCI de la tarjeta: su módulo de kernel `nvidia.ko` contiene el núcleo cerrado de NVIDIA (`nv-kernel.o_binary`), enlazado sin modificar con el código de interfaz, con licencia MIT, del paquete del controlador, y sus bibliotecas de espacio de usuario se toman sin modificar de ese paquete. Se rigen por el NVIDIA Driver License Agreement (`licenses/LicenseRef-NVIDIA-Driver-License-Agreement.txt`, también en `/usr/share/licenses/nvidia-legacy` del sistema en marcha). No hay código fuente del núcleo cerrado. Hace la imagen unos 120 MB más grande.

## Código fuente

El código fuente completo de esta compilación es el árbol de LibreELEC Hades en el commit que indica `BUILD_ID` en `/etc/os-release` del sistema en marcha, junto con los archivos de fuentes que nombran las recetas de ese árbol, cada uno con el SHA-256 que figura en ellas.

Ese árbol está publicado en el repositorio del proyecto, que es donde se ofrecen sus imágenes. Si la imagen te la dio otra persona, tiene que darte también ese código, o decirte dónde se ofrece: pídeselo.

El instalador para Windows que se ofrece con las imágenes es parte del proyecto (GPL-2.0-or-later); su código fuente está en ese árbol, en `tools/hades-installer`.

## Marcas

Kodi es una marca registrada de la XBMC Foundation. Dolby, Dolby Vision y Dolby Atmos son marcas de Dolby Laboratories. NVIDIA, GeForce y RTX son marcas de NVIDIA Corporation. AMD, Radeon y Ryzen son marcas de Advanced Micro Devices, Inc. Intel es una marca de Intel Corporation. HDMI es una marca de HDMI Licensing Administrator, Inc. HDR10+ es una marca de HDR10+ Technologies, LLC. LibreELEC es el nombre del proyecto de Team LibreELEC. Los demás nombres pertenecen a sus dueños.

Estos nombres se usan solo para decir en qué se basa este software y con qué funciona. Usarlos no significa que ninguno de sus dueños apruebe, patrocine o certifique LibreELEC Hades.

## Sin garantía

Este software se entrega "tal cual", sin garantía de ningún tipo, en la medida en que la ley lo permita. Véanse las secciones 11 y 12 de la GNU General Public License, versión 2, y las secciones 15 y 16 de la versión 3.
