# mutant of ffmpeg-9977 for the stage slkeep of run-ffmpeg.sh: the context keeps the software format that
# export_stream_params() writes, not the hardware decoder's
FILE = 'libavcodec/hevc/hevcdec.c'
PAIRS = [('                s->avctx->pix_fmt = hw_pix_fmt;\n', '                (void)hw_pix_fmt;\n')]
