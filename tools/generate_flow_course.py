#!/usr/bin/env python3
"""FLOW CIRCUIT generator.

An original course designed for this car's drift-first handling, feel first.
The centerline is a periodic Catmull-Rom spline through hand-placed
waypoints (see WAYPOINTS), so the loop always closes exactly. Headings and
curvature are derived numerically from the spline and smoothed.

Design notes:
  - half_width 10 m (Suzuka: 6.5) and runoff 16 m: walls stay far while sliding
  - big-radius sweepers in the main flow, two linked S sections for drift
    chains, one tight hairpin for deep slow angles
  - gentle elevation waves only; banking leans into corners

Usage: python3 tools/generate_flow_course.py [output]
  Default output: assets/track/flow.txt
"""

import math
import os
import sys

# Waypoints (x, z). Traced clockwise on the page (x right, z down):
# top straight -> Flow In -> right side down -> Twin S -> hairpin at the
# bottom -> Aqua Sweeper back west -> Drift Link -> Final Flow -> top.
WAYPOINTS = [
    (124.0, 93.0),   # 0  top straight (start line area)
    (512.0, 85.0),   # 1  top straight
    (868.0, 109.0),  # 2  top straight, gentle flow
    (1178.0, 171.0), # 3  Flow In
    (1395.0, 357.0), # 4  right side
    (1442.0, 589.0), # 5  Twin S in
    (1333.0, 729.0), # 6  Twin S out
    (1085.0, 806.0), # 7  bottom right
    (868.0, 775.0),  # 8  hairpin approach
    (790.0, 818.0),  # 9  hairpin dive
    (706.0, 772.0),  # 10 hairpin apex
    (730.0, 700.0),  # 11 hairpin exit
    (600.0, 655.0),  # 12 hairpin exit straight
    (400.0, 640.0),  # 13 Aqua Sweeper (long, back west)
    (217.0, 558.0),  # 14 Crescent
    (120.0, 450.0),  # 15 Drift Link in
    (185.0, 320.0),  # 16 Drift Link out
    (109.0, 217.0),  # 17 Final Flow
]

NAMES = [
    "Main Straight",   # 0
    "Flow In",         # 1
    "Link Straight",   # 2
    "Twin S",          # 3
    "Rise Straight",   # 4
    "Hairpin",         # 5
    "Hairpin Exit",    # 6
    "Aqua Sweeper",    # 7
    "Crescent",        # 8
    "Flip",            # 9
    "Drift Link",      # 10
    "Back Straight",   # 11
    "Final S",         # 12
    "Final Flow",      # 13
]

# Per-span (from waypoint i to i+1): (nameId, corner number)
SPANS = [
    (0, 0),    # 0->1   main straight
    (0, 0),    # 1->2   main straight
    (1, 1),    # 2->3   Flow In
    (1, 1),    # 3->4   Flow In
    (3, 2),    # 4->5   Twin S first
    (3, 3),    # 5->6   Twin S second
    (4, 0),    # 6->7   rise
    (5, 4),    # 7->8   hairpin
    (5, 4),    # 8->9   hairpin
    (5, 4),    # 9->10  hairpin
    (5, 4),    # 10->11 hairpin
    (6, 0),    # 11->12 hairpin exit
    (7, 5),    # 12->13 Aqua Sweeper
    (8, 6),    # 13->14 Crescent
    (10, 7),   # 14->15 Drift Link first
    (10, 8),   # 15->16 Drift Link second
    (13, 9),   # 16->17 Final Flow
    (13, 9),   # 17->0  Final Flow
]


HALF_WIDTH = 10.0
CURB_WIDTH = 0.8
RUNOFF = 16.0
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
        os.path.dirname(os.path.abspath(__file__)), "..", "assets", "track", "flow.txt")
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

    # Rotate so sample 0 is on the top straight (nearest to (200, 57)).
    start = min(range(n), key=lambda i: (samples[i][0] - 330.0) ** 2 + (samples[i][1] - 89.0) ** 2)
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
        return 4.0 * math.sin(2.0 * math.pi * 2.0 * u + 1.0) + 1.8 * math.sin(2.0 * math.pi * 5.0 * u)

    lines = []
    lines.append("# Flow Circuit - original course designed for this car (feel first)")
    lines.append("# generated by tools/generate_flow_course.py - do not edit by hand")
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
