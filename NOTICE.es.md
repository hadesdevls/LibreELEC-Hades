# AVISO sobre LibreELEC Hades

(In English: NOTICE.md)

## Qué es

LibreELEC Hades es una compilación no oficial de LibreELEC para PC de 64 bits (Generic x86_64), con
cambios. Lo principal que añade es la salida nativa de Dolby Vision por HDMI desde Kodi en un PC, con
una tarjeta gráfica o con la gráfica integrada en el procesador. README.es.md, en el repositorio del proyecto y en el archivo de la publicación, dice
qué hace y qué se ha probado.

**No es LibreELEC y no es oficial.** LibreELEC Hades no está hecho, mantenido, avalado ni certificado
por Team LibreELEC, por Team Kodi ni la XBMC Foundation, por Dolby Laboratories, ni por NVIDIA, AMD o
Intel, y no tiene relación con ninguno de ellos. No les pidas ayuda con él. El centro multimedia de
esta compilación es una versión modificada de Kodi; no lo suministra ni lo avala la XBMC Foundation, ni
es un producto suyo, y no se presenta como Kodi: su pantalla dice "LibreELEC Hades", y una página de
su información del sistema dice qué es.

La salida de Dolby Vision es una implementación independiente que sigue una especificación pública
(ETSI GS CCM 001). No contiene código de Dolby, y Dolby no la ha probado ni certificado.

## Trabajo de otros sobre el que se apoya

- **LibreELEC** (Team LibreELEC, y antes OpenELEC): la distribución entera y su sistema de compilación.
  El código original de LibreELEC está bajo la GNU General Public License, versión 2.
  https://github.com/LibreELEC/LibreELEC.tv
- **Kodi** (Team Kodi y sus colaboradores): el centro multimedia. GPL-2.0-or-later.
  https://github.com/xbmc/xbmc
- **El fork de LibreELEC con Dolby Vision de CroqueMr**, y quienes colaboran en él: de ahí parte
  este trabajo. Se usan, tal como los publicó o adaptados, su módulo de Dolby Vision para Kodi
  (`dvbridge`, GPL-3.0-or-later), su salida de Dolby Vision para gráficas Intel en el kernel de Linux y
  sus cambios en FFmpeg y libplacebo. https://github.com/CroqueMr/libreelec-x86-DV
- **FFmpeg**, **libplacebo** y el **kernel de Linux**, con parches.
- **Dom Cobley (popcornmix)**: la decodificación de la segunda vista del vídeo H.264 MVC (Blu-ray 3D),
  su serie de cambios al decodificador H.264 de FFmpeg (pull request 24303 de FFmpeg,
  LGPL-2.1-or-later), aplicada aquí a una copia de ese decodificador; y, en Kodi (GPL-2.0-or-later),
  cómo se leen a la vez los dos clips de un disco 3D y un cambio en cómo se leen los modos 3D de una
  pantalla, tal como él los escribió.
- **El árbol de LibreELEC de cinema-ONE**: cuatro cambios al controlador de gráficas AMD del kernel de
  Linux para el 3D por HDMI, usados aquí con arreglos. https://github.com/cinema-ONE/LibreELEC.tv
- **Los módulos de kernel abiertos de NVIDIA** (NVIDIA Corporation, MIT/GPL-2.0), compilados desde su
  código publicado con tres parches, y **nvidia-vaapi-driver** (MIT).
- **Jost**, una tipografía de indestructible type* (SIL Open Font License 1.1): con ella está escrito
  el nombre de la compilación en sus imágenes, que dibuja un guion de este proyecto
  (`packages/mediacenter/kodi/brand`).
- **BlueZ**: arreglos de su propio repositorio, aplicados como parches que conservan a sus autores.
- **dovi_tool**, de quietvoid (MIT): referencia para la sintaxis de los metadatos de Dolby Vision.
- **OpenSSL 4.0.3**: el cambio a la receta de LibreELEC es de Rudi Heitbaum, de la pull request 11104
  de LibreELEC, tomado como su commit.
- Todo lo demás viene de LibreELEC sin cambios: unos 290 paquetes, cada uno con su licencia.

Cada parche de esta compilación dice en su cabecera de dónde viene, qué hace y por qué.

## Licencias

- Los textos de las licencias están en la carpeta `licenses`, junto a este archivo.
- Como el módulo de Dolby Vision añadido a Kodi es GPL-3.0-or-later, el programa Kodi de esta
  compilación se distribuye bajo GPL-3.0-or-later.
- La licencia de cada paquete figura en su receta (`PKG_LICENSE` en su `package.mk`).

## Partes que no son software libre

Como las imágenes de LibreELEC, esta contiene firmware para muchos dispositivos y microcódigo de
procesadores Intel, con sus propias condiciones. Contiene además:

- **Bibliotecas de espacio de usuario y firmware de NVIDIA**, copyright de NVIDIA Corporation, tomados
  sin modificar del instalador del controlador de NVIDIA y solo para usarse con tarjetas gráficas
  NVIDIA. Se rigen por el **NVIDIA Driver License Agreement**, cuyo texto es
  `licenses/LicenseRef-NVIDIA-Driver-License-Agreement.txt`. No hay código fuente disponible de ellos.

## Código fuente

El código fuente completo de esta compilación es el árbol de LibreELEC Hades en el commit que indica
`BUILD_ID` en `/etc/os-release` del sistema en marcha, junto con los archivos de fuentes que nombran
las recetas de ese árbol, cada uno con el SHA-256 que figura en ellas.

Ese árbol está publicado en el repositorio del proyecto, que es donde se ofrecen sus imágenes. Si la
imagen te la dio otra persona, tiene que darte también ese código, o decirte dónde se ofrece:
pídeselo.

El instalador para Windows que se ofrece con las imágenes es parte del proyecto (GPL-2.0-or-later); su
código fuente está en ese árbol, en `tools/hades-installer`.

## Marcas

Kodi es una marca registrada de la XBMC Foundation. Dolby, Dolby Vision y Dolby Atmos son marcas de
Dolby Laboratories. NVIDIA, GeForce y RTX son marcas de NVIDIA Corporation. AMD, Radeon y Ryzen son
marcas de Advanced Micro Devices, Inc. Intel es una marca de Intel Corporation. HDMI es una marca de
HDMI Licensing Administrator, Inc. LibreELEC es el nombre del proyecto de Team LibreELEC. Los demás
nombres pertenecen a sus dueños.

Estos nombres se usan solo para decir en qué se basa este software y con qué funciona. Usarlos no
significa que ninguno de sus dueños apruebe, patrocine o certifique LibreELEC Hades.

## Sin garantía

Este software se entrega "tal cual", sin garantía de ningún tipo, en la medida en que la ley lo
permita. Véanse las secciones 11 y 12 de la GNU General Public License, versión 2, y las secciones 15
y 16 de la versión 3.
