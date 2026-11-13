#!/usr/bin/env python3
"""
Writes the two IGES samples in this directory. Re-run to regenerate:

    python3 make_samples.py

The samples are built entity by entity here rather than exported by the
framework's own IGES writer: a reader tested only against files its own writer
produced proves nothing. This script shares no code with the C++ writer, and
it writes in the habits other exporters have and that writer does not - the
default delimiters given as empty global fields, a D exponent, a circle placed
by a transformation matrix that mirrors it, colours by number and by
definition, trimming curves in parameter space beside the model-space ones.

    Block.igs     a 10 x 20 x 30 box as a manifold solid B-rep object (186)
                  with its full topology: vertex list, edge list, six loops,
                  six faces on plane surfaces (190), one closed shell; named
                  "Block" by a name property, one face coloured.
    Bracket.igs   trimmed surfaces (144), no topology: a 40 x 30 plate on a
                  B-spline surface with a radius 5 hole in it, and the open
                  tube of a radius 5, height 10 boss on a cylinder (192).

What each one must measure is asserted in Tests/ModelIgesTest.cpp.
"""
import math
import os


def real(value):
    text = repr(float(value))
    if "e" in text:
        mantissa, exponent = text.split("e")
        if "." not in mantissa:
            mantissa += "."
        return mantissa + "E" + exponent
    if text.endswith(".0"):
        text = text[:-1]
    return text


def hollerith(text):
    return f"{len(text)}H{text}"


class Deck:
    def __init__(self):
        self.entities = []   # (type, form, params, transform, colour, status, label)

    def add(self, type_, params, form=0, transform=0, colour=0, status="00010000", label=""):
        self.entities.append((type_, form, params, transform, colour, status, label))
        return len(self.entities) * 2 - 1

    def write(self, path, start, global_params):
        def pack(tokens, width):
            lines, current = [], ""
            for i, token in enumerate(tokens):
                piece = token + (";" if i == len(tokens) - 1 else ",")
                if len(current) + len(piece) > width:
                    lines.append(current)
                    current = ""
                current += piece
            lines.append(current)
            return lines

        out = []
        start_lines = [start[i:i + 72] for i in range(0, len(start), 72)]
        for n, line in enumerate(start_lines, 1):
            out.append(f"{line:<72}S{n:7d}")
        global_lines = pack(global_params, 72)
        for n, line in enumerate(global_lines, 1):
            out.append(f"{line:<72}G{n:7d}")

        params_lines, first, count = [], [], []
        for index, (type_, form, params, *_rest) in enumerate(self.entities):
            directory = index * 2 + 1
            first.append(len(params_lines) + 1)
            lines = pack([str(type_)] + params, 64)
            count.append(len(lines))
            for line in lines:
                params_lines.append(f"{line:<64} {directory:7d}")

        for index, (type_, form, params, transform, colour, status, label) in enumerate(self.entities):
            directory = index * 2 + 1
            out.append(f"{type_:8d}{first[index]:8d}{0:8d}{0:8d}{0:8d}{0:8d}{transform:8d}{0:8d}"
                       f"{status:>8}D{directory:7d}")
            out.append(f"{type_:8d}{0:8d}{colour:8d}{count[index]:8d}{form:8d}{'':8}{'':8}"
                       f"{label:>8}{0:8d}D{directory + 1:7d}")
        for n, line in enumerate(params_lines, 1):
            out.append(f"{line}P{n:7d}")
        terminate = (f"S{len(start_lines):7d}G{len(global_lines):7d}"
                     f"D{len(self.entities) * 2:7d}P{len(params_lines):7d}")
        out.append(f"{terminate:<72}T{1:7d}")
        for line in out:
            assert len(line) == 80, (len(line), line)
        with open(path, "w", newline="\n") as f:
            f.write("\n".join(out) + "\n")
        print(f"wrote {path}: {len(self.entities)} entities")


def global_section(product, file_name, unit_flag, unit_name, default_delimiters):
    head = ["", ""] if default_delimiters else ["1H,", "1H;"]
    return head + [hollerith(product), hollerith(file_name), hollerith("make_samples.py"),
                   hollerith("1.0"), "32", "38", "6", "308", "15", hollerith(product), "1.0",
                   str(unit_flag), hollerith(unit_name), "1", "0.1",
                   hollerith("20261112.000000"), "1.0D-6", "50.0", hollerith("UltraCanvas"),
                   hollerith("hand-authored"), "11", "0", hollerith("20261112.000000")]


def block(path):
    deck = Deck()
    size = (10.0, 20.0, 30.0)

    def corner(i, j, k):
        return (size[0] * i, size[1] * j, size[2] * k)

    def index(i, j, k):
        return 1 + i + 2 * j + 4 * k

    vertices = [corner(i, j, k) for k in (0, 1) for j in (0, 1) for i in (0, 1)]
    params = [str(len(vertices))]
    for v in vertices:
        params += [real(c) for c in v]
    vertex_list = deck.add(502, params, form=1)

    # Twelve edges, each from its lower-numbered vertex to its higher one.
    pairs = []
    for k in (0, 1):
        for j in (0, 1):
            pairs.append((index(0, j, k), index(1, j, k)))
        for i in (0, 1):
            pairs.append((index(i, 0, k), index(i, 1, k)))
    for j in (0, 1):
        for i in (0, 1):
            pairs.append((index(i, j, 0), index(i, j, 1)))
    lines = []
    for a, b in pairs:
        p, q = vertices[a - 1], vertices[b - 1]
        lines.append(deck.add(110, [real(c) for c in p + q]))
    params = [str(len(pairs))]
    for (a, b), line in zip(pairs, lines):
        params += [str(line), str(vertex_list), str(a), str(vertex_list), str(b)]
    edge_list = deck.add(504, params, form=1)

    def edge_use(a, b):
        for n, pair in enumerate(pairs, 1):
            if pair == (a, b):
                return n, 1
            if pair == (b, a):
                return n, 0
        raise ValueError((a, b))

    # Each face counter-clockwise seen from outside, with its outward normal.
    faces = [
        ([(0, 0, 0), (0, 1, 0), (1, 1, 0), (1, 0, 0)], (0, 0, -1)),
        ([(0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)], (0, 0, 1)),
        ([(0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)], (0, -1, 0)),
        ([(0, 1, 0), (0, 1, 1), (1, 1, 1), (1, 1, 0)], (0, 1, 0)),
        ([(0, 0, 0), (0, 0, 1), (0, 1, 1), (0, 1, 0)], (-1, 0, 0)),
        ([(1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1)], (1, 0, 0)),
    ]
    paint = deck.add(314, ["12.5", "45.", "90.", hollerith("Blue anodised")], status="00000200")
    face_ids = []
    for number, (corners, normal) in enumerate(faces):
        ids = [index(*c) for c in corners]
        uses = []
        for n in range(4):
            edge, forward = edge_use(ids[n], ids[(n + 1) % 4])
            uses += ["0", str(edge_list), str(edge), str(forward), "0"]
        loop = deck.add(508, ["4"] + uses, form=1)
        origin = deck.add(116, [real(c) for c in corner(*corners[0])])
        axis = deck.add(123, [real(c) for c in normal])
        reference_vector = tuple(b - a for a, b in zip(corner(*corners[0]), corner(*corners[1])))
        length = math.sqrt(sum(c * c for c in reference_vector))
        reference = deck.add(123, [real(c / length) for c in reference_vector])
        plane = deck.add(190, [str(origin), str(axis), str(reference)], form=1)
        colour = -paint if number == 1 else 0
        face_ids.append(deck.add(510, [str(plane), "1", "1", str(loop)], form=1, colour=colour))
    shell_params = [str(len(face_ids))]
    for face in face_ids:
        shell_params += [str(face), "1"]
    shell = deck.add(514, shell_params, form=1)
    name = deck.add(406, ["1", hollerith("Block")], form=15)
    deck.add(186, [str(shell), "1", "0", "0", "1", str(name)], status="00000000", label="MSBO")
    deck.write(path, "A 10 x 20 x 30 block as a manifold solid B-rep object, built by "
               "make_samples.py.", global_section("Block", "Block.igs", 2, "MM", False))


def bracket(path):
    deck = Deck()
    # --- the plate: a degree 1 x 1 B-spline patch whose (u, v) are its (x, y) ---
    plate = deck.add(128, ["1", "1", "1", "1", "0", "0", "1", "0", "0",
                           "0.", "0.", "40.", "40.", "0.", "0.", "30.", "30.",
                           "1.", "1.", "1.", "1.",
                           "0.", "0.", "0.", "40.", "0.", "0.",
                           "0.", "30.", "0.", "40.", "30.", "0.",
                           "0.", "40.", "0.", "30."])
    corners = [(0.0, 0.0), (40.0, 0.0), (40.0, 30.0), (0.0, 30.0)]
    model, parameter = [], []
    for n in range(4):
        (x0, y0), (x1, y1) = corners[n], corners[(n + 1) % 4]
        model.append(deck.add(110, [real(x0), real(y0), "0.", real(x1), real(y1), "0."]))
        parameter.append(deck.add(110, [real(x0), real(y0), "0.", real(x1), real(y1), "0."],
                                  status="00010500"))
    model_loop = deck.add(102, ["4"] + [str(c) for c in model])
    parameter_loop = deck.add(102, ["4"] + [str(c) for c in parameter], status="00010500")
    outer = deck.add(142, ["1", str(plate), str(parameter_loop), str(model_loop), "1"])
    # The hole, clockwise as a hole is in a face whose outer loop runs
    # counter-clockwise: written as a counter-clockwise arc and left to the
    # reader, which does not care which way a hole runs.
    hole_model = deck.add(100, ["0.", "20.", "15.", "25.", "15.", "25.", "15."])
    hole_parameter = deck.add(100, ["0.", "20.", "15.", "25.", "15.", "25.", "15."],
                              status="00010500")
    hole = deck.add(142, ["1", str(plate), str(hole_parameter), str(hole_model), "1"])
    paint = deck.add(314, ["80.", "80.", "85.", hollerith("Aluminium")], status="00000200")
    deck.add(144, [str(plate), "1", "1", str(outer), str(hole)], status="00000000",
             colour=-paint, label="PLATE")

    # --- the boss: the side of a cylinder, radius 5, from z = 0 to z = 1.0D1 ---
    location = deck.add(116, ["20.", "15.", "0."])
    axis = deck.add(123, ["0.", "0.", "1."])
    reference = deck.add(123, ["1.", "0.", "0."])
    cylinder = deck.add(192, [str(location), str(axis), "5.", str(reference)], form=1)
    bottom = deck.add(100, ["0.", "20.", "15.", "25.", "15.", "25.", "15."])
    seam_up = deck.add(110, ["25.", "15.", "0.", "25.", "15.", "1.0D1"])
    # The top circle runs the other way round. An arc only runs
    # counter-clockwise in its own plane, so it is placed by a matrix that
    # mirrors y and z: (x, y, z) -> (x, -y, -z).
    mirror = deck.add(124, ["1.", "0.", "0.", "0.", "0.", "-1.", "0.", "0.",
                            "0.", "0.", "-1.", "0."])
    top = deck.add(100, ["-10.", "20.", "-15.", "25.", "-15.", "25.", "-15."], transform=mirror)
    seam_down = deck.add(110, ["25.", "15.", "10.", "25.", "15.", "0."])
    side = deck.add(102, ["4", str(bottom), str(seam_up), str(top), str(seam_down)])
    boundary = deck.add(142, ["0", str(cylinder), "0", str(side), "2"])
    deck.add(144, [str(cylinder), "1", "0", str(boundary)], status="00000000", colour=2,
             label="BOSS")

    deck.write(path, "A plate with a hole and a boss as trimmed surfaces, built by "
               "make_samples.py.", global_section("Bracket", "Bracket.igs", 2, "MM", True))


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    block(os.path.join(here, "Block.igs"))
    bracket(os.path.join(here, "Bracket.igs"))
