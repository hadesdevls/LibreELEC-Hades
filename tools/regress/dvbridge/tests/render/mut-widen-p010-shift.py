# the code at the bottom of 16 bits (as YUV420P10), not at the top (P010): (v << 2), not (v << 2) << 6
PAIRS = [(r'(v * 256.0 + 0.25)', r'(v * 4.0 + 0.25)')]
