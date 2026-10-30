#!/usr/bin/env python3
"""COAST CIRCUIT generator.

A flowing coastal course in the spirit of the reference video: long
straights and fast sweepers woven together, close walls and (with the
renderer's scenery) trees flashing past for speed sensation.
The centerline is a periodic Catmull-Rom spline through hand-placed
waypoints (see WAYPOINTS), so the loop always closes exactly. Headings and
curvature are derived numerically from the spline and smoothed.

Design notes:
  - half_width 9 m, runoff only 3.5 m: the wall sits at the roadside
    like the video's barriers, a constant speed reference
  - two long straights (800 m / 600 m) linked by fast sweepers,
    a pair of medium bends, one hairpin for contrast
  - gentle elevation waves only; banking leans into corners

Usage: python3 tools/generate_coast_course.py [output]
  Default output: assets/track/coast.txt
"""

import math
import os
import sys

# Waypoints (x, z). Traced clockwise on the page (x right, z down):
# top main straight -> Coast Sweeper down the east side -> Palm Bends ->
# bottom Cliff Straight west -> Twin Bends -> hairpin notch -> Ridge
# Climb -> Sunset Straight -> Final Sweep -> top.
WAYPOINTS = [
    (430.0, 55.0),    # 0  main straight (start area)
    (700.0, 30.0),    # 1  main straight
    (942.0, 51.0),    # 2  main straight end
    (1296.0, 161.0),  # 3  Coast Sweeper in
    (1532.0, 415.0),  # 4  Coast Sweeper out / east side
    (1514.0, 674.0),  # 5  Palm Bends in
    (1284.0, 851.0),  # 6  Palm Bends out
    (942.0, 908.0),   # 7  Cliff Straight
    (535.0, 881.0),   # 8  Cliff Straight end
    (281.0, 775.0),   # 9  Twin Bends in
    (234.0, 562.0),   # 10 Twin Bends out
    (275.0, 380.0),   # 11 hairpin approach
    (105.0, 340.0),   # 12 hairpin dive
    (30.0, 270.0),    # 13 hairpin apex
    (105.0, 185.0),   # 14 hairpin exit
    (281.0, 137.0),   # 15 Ridge Climb
]

NAMES = [
    "Main Straight",   # 0
    "Coast Sweeper",   # 1
    "Palm Bends",      # 2
    "Cliff Straight",  # 3
    "Twin Bends",      # 4
    "Hairpin",         # 5
    "Hairpin Exit",    # 6
    "Ridge Climb",     # 7
    "Sunset Straight", # 8
    "Final Sweep",     # 9
]

# Per-span (from waypoint i to i+1): (nameId, corner number)
SPANS = [
    (0, 0),  # 0->1   main straight
    (0, 0),  # 1->2   main straight
    (1, 1),  # 2->3   Coast Sweeper
    (1, 1),  # 3->4   Coast Sweeper
    (2, 2),  # 4->5   Palm Bends
    (2, 3),  # 5->6   Palm Bends
    (3, 0),  # 6->7   Cliff Straight
    (3, 0),  # 7->8   Cliff Straight
    (4, 4),  # 8->9   Twin Bends
    (4, 5),  # 9->10  Twin Bends
    (5, 6),  # 10->11 Hairpin
    (5, 6),  # 11->12 Hairpin
    (5, 6),  # 12->13 Hairpin
    (5, 6),  # 13->14 Hairpin
    (6, 0),  # 14->15 Hairpin Exit
    (9, 7),  # 15->0  Final Sweep
]


HALF_WIDTH = 9.0
CURB_WIDTH = 0.8
RUNOFF = 3.5
DS = 2.0  # sample spacing (m)


def catmull_rom_closed(points, subdiv):
    """Dense polyline [(x, z, span_index)] around the closed spline."""
    n = len(points)
    out = []
    for i in range(n):
        p0 = points[(i - 1) % n]
        p1 = points[i]
        p2 = points[(i + 1) % n]
        p3 = points[(i + 2) % n]
        for j in range(subdiv):
            t = j / subdiv
            t2 = t * t
            t3 = t2 * t
            x = 0.5 * ((2 * p1[0]) + (-p0[0] + p2[0]) * t +
                       (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2 +
                       (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3)
            z = 0.5 * ((2 * p1[1]) + (-p0[1] + p2[1]) * t +
                       (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2 +
                       (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3)
            out.append((x, z, i))
    return out


def resample(dense, ds):
    """Walk the dense polyline and emit points at exact ds intervals
    (interpolated, so the emitted spacing really is ds)."""
    pts = [dense[0]]
    prev = dense[0]
    acc = 0.0  # distance from the last emitted point to prev
    for p in dense[1:] + [dense[0]]:
        seg = math.hypot(p[0] - prev[0], p[1] - prev[1])
        while acc + seg >= ds and seg > 0.0:
            need = ds - acc
            f = need / seg
            q = (prev[0] + (p[0] - prev[0]) * f,
                 prev[1] + (p[1] - prev[1]) * f, prev[2])
            pts.append(q)
            seg -= need
            prev = q
            acc = 0.0
        acc += seg
        prev = p
    return pts


def smooth_circular(values, radius):
    n = len(values)
    out = []
    for i in range(n):
        s = 0.0
        for k in range(-radius, radius + 1):
            s += values[(i + k) % n]
        out.append(s / (2 * radius + 1))
    return out


def wrap_pi(a):
    while a > math.pi:
        a -= 2 * math.pi
    while a < -math.pi:
        a += 2 * math.pi
    return a


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "assets", "track", "coast.txt")
    dense = catmull_rom_closed(WAYPOINTS, 60)
    samples = resample(dense, DS)
    n = len(samples)
    total = n * DS

    # Headings from central differences, then curvature, both smoothed.
    headings = []
    for i in range(n):
        ax, az, _ = samples[(i - 3) % n]
        bx, bz, _ = samples[(i + 3) % n]
        headings.append(math.atan2(bx - ax, bz - az))
    raw_k = []
    for i in range(n):
        dh = wrap_pi(headings[(i + 1) % n] - headings[(i - 1) % n])
        raw_k.append(dh / (2 * DS))
    kappa = smooth_circular(raw_k, 4)
    min_radius = 1.0 / max(abs(k) for k in kappa)

    # Rotate so sample 0 is on the main straight (nearest to (260, 40)).
    start = min(range(n), key=lambda i: (samples[i][0] - 520.0) ** 2 + (samples[i][1] - 45.0) ** 2)
    samples = samples[start:] + samples[:start]
    kappa = kappa[start:] + kappa[:start]
    headings = headings[start:] + headings[:start]

    # Non-adjacent centerline clearance (self-intersection / proximity check)
    min_gap = 1e9
    for i in range(n):
        xi, zi, _ = samples[i]
        for j in range(i + 40, n):
            if i < 40 and j > n - 40:
                continue
            dx = samples[j][0] - xi
            dz = samples[j][1] - zi
            if dx * dx + dz * dz < min_gap:
                min_gap = dx * dx + dz * dz
    print(f"samples: {n}, length: {total:.0f} m, min radius: {min_radius:.0f} m, "
          f"min non-adjacent gap: {math.sqrt(min_gap):.0f} m")

    back = int(round(90.0 / DS))
    sx, sz, _ = samples[n - back]
    spawn_yaw = headings[n - back]

    def elev(s):
        u = s / total
        return 3.0 * math.sin(2.0 * math.pi * 2.0 * u + 1.0) + 1.4 * math.sin(2.0 * math.pi * 5.0 * u)

    lines = []
    lines.append("# Coast Circuit - long straights and fast sweepers, walls close for speed feel")
    lines.append("# generated by tools/generate_coast_course.py - do not edit by hand")
    lines.append("origin 0.0 0.0")
    lines.append(f"half_width {HALF_WIDTH:.2f}")
    lines.append(f"curb_width {CURB_WIDTH:.2f}")
    lines.append(f"runoff {RUNOFF:.2f}")
    lines.append(f"length {total:.1f}")
    lines.append(f"spawn {sx:.3f} {elev(total - 90.0):.3f} {sz:.3f} {spawn_yaw:.6f}")
    lines.append(f"names {len(NAMES)}")
    for i, name in enumerate(NAMES):
        lines.append(f"{i} {name}")
    lines.append(f"samples {n}")
    for i, (x, z, span) in enumerate(samples):
        name_id, corner_no = SPANS[span]
        y = elev(i * DS)
        cross = max(-0.06, min(0.06, -kappa[i] * 14.0))
        lines.append(f"{x:.3f} {z:.3f} {y:.3f} {cross:.4f} 0 {corner_no} {name_id}")
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
