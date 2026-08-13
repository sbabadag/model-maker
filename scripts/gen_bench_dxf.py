"""Generate a DXF file with N random line entities for rendering benchmark."""
import random, sys, math

def gen_dxf(n: int, out_path: str):
    random.seed(42)
    w, h = 10000.0, 10000.0  # bounding box
    lines = []
    lines.append("0\nSECTION\n2\nHEADER\n0\nENDSEC\n")
    lines.append("0\nSECTION\n2\nTABLES\n0\nENDSEC\n")
    lines.append("0\nSECTION\n2\nBLOCKS\n0\nENDSEC\n")
    lines.append("0\nSECTION\n2\nENTITIES\n")
    for _ in range(n):
        x1, y1 = random.uniform(0, w), random.uniform(0, h)
        angle = random.uniform(0, 2 * math.pi)
        length = random.uniform(10, 500)
        x2, y2 = x1 + length * math.cos(angle), y1 + length * math.sin(angle)
        lines.append(f"0\nLINE\n8\n0\n10\n{x1:.4f}\n20\n{y1:.4f}\n30\n0.0\n")
        lines.append(f"11\n{x2:.4f}\n21\n{y2:.4f}\n31\n0.0\n")
    lines.append("0\nENDSEC\n0\nEOF\n")
    with open(out_path, 'w') as f:
        f.write(''.join(lines))
    print(f"Generated {out_path} with {n} LINE entities")

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    gen_dxf(n, sys.argv[2] if len(sys.argv) > 2 else "bench_lines.dxf")
