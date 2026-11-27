# mutant of ffmpeg-9977 for the stage slkeep of run-ffmpeg.sh: every change of the SPS keeps the hardware decoder
FILE = 'libavcodec/hevc/ps.c'
PAIRS = [('    same = !memcmp(tmp, ref, sizeof(*ref));\n', '    same = 1;\n')]
