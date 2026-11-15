"""Pack the Core2 Clawd print files into ready-to-print 3MF plates.

Each plate holds its parts already oriented (checked by make_clawd_core2.py) and
placed inside a 180 x 180 mm area, so it fits every common bed (A1 mini and up).
Per-part settings are written for Bambu Studio / Orca (Metadata/model_settings.config)
and PrusaSlicer (Metadata/Slic3r_PE_model.config): no supports, 4 walls, 15 % gyroid,
0.2 mm layers, brim off. Printer and filament profiles come from your slicer.
"""
import os, zipfile, trimesh
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__)) + '/'
AREA = 180.0

SETTINGS = {   # key: (bambu/orca, prusaslicer)
    "supports":  (("enable_support", "0"),               ("support_material", "0")),
    "walls":     (("wall_loops", "4"),                   ("perimeters", "4")),
    "infill":    (("sparse_infill_density", "15%"),      ("fill_density", "15%")),
    "pattern":   (("sparse_infill_pattern", "gyroid"),   ("fill_pattern", "gyroid")),
    "layer":     (("layer_height", "0.2"),               ("layer_height", "0.2")),
    "brim":      (("brim_type", "no_brim"),              ("brim_width", "0")),
}

def mesh_xml(m):
    v = "\n".join(f'<vertex x="{x:.4f}" y="{y:.4f}" z="{z:.4f}"/>' for x, y, z in m.vertices)
    t = "\n".join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in m.faces)
    return f"<mesh><vertices>\n{v}\n</vertices><triangles>\n{t}\n</triangles></mesh>"

def write(out, parts):
    """parts: [(name, stl_path)]. Laid out left to right, centered in AREA."""
    meshes = [(n, trimesh.load(HERE + p)) for n, p in parts]
    gap = 10.0
    total = sum(m.extents[0] for _, m in meshes) + gap * (len(meshes) - 1)
    depth = max(m.extents[1] for _, m in meshes)
    assert total <= AREA and depth <= AREA, "plate too full"
    x = (AREA - total) / 2
    objs, items, bambu, prusa = [], [], [], []
    for i, (name, m) in enumerate(meshes, start=1):
        m = m.copy(); m.apply_translation(-m.bounds[0])          # sits on z=0, corner at origin
        y = (AREA - m.extents[1]) / 2
        objs.append(f'<object id="{i}" name="{escape(name)}" type="model">{mesh_xml(m)}</object>')
        items.append(f'<item objectid="{i}" transform="1 0 0 0 1 0 0 0 1 {x:.3f} {y:.3f} 0" printable="1"/>')
        x += m.extents[0] + gap
        b = "".join(f'<metadata key="{k}" value="{v}"/>' for (k, v), _ in SETTINGS.values())
        bambu.append(f'<object id="{i}"><metadata key="name" value="{escape(name)}"/><metadata key="extruder" value="1"/>{b}'
                     f'<part id="1" subtype="normal_part"><metadata key="name" value="{escape(name)}"/></part></object>')
        p = "".join(f'<metadata type="object" key="{k}" value="{v}"/>' for _, (k, v) in SETTINGS.values())
        prusa.append(f'<object id="{i}" instances_count="1"><metadata type="object" key="name" value="{escape(name)}"/>{p}'
                     f'<volume firstid="0" lastid="{len(m.faces) - 1}"><metadata type="volume" key="name" value="{escape(name)}"/></volume></object>')
    model = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">\n'
             '<metadata name="Title">Clawd desk buddy (M5Stack Core2)</metadata>\n'
             '<metadata name="Designer">TSSAI</metadata>\n'
             f'<resources>\n{"".join(objs)}\n</resources>\n<build>\n{"".join(items)}\n</build>\n</model>\n')
    with zipfile.ZipFile(HERE + out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml",
                   '<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
                   '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
                   '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>'
                   '<Default Extension="config" ContentType="text/xml"/></Types>')
        z.writestr("_rels/.rels",
                   '<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
                   '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
        z.writestr("3D/3dmodel.model", model)
        z.writestr("Metadata/model_settings.config", '<?xml version="1.0" encoding="UTF-8"?>\n<config>\n' + "\n".join(bambu) + "\n</config>\n")
        z.writestr("Metadata/Slic3r_PE_model.config", '<?xml version="1.0" encoding="UTF-8"?>\n<config>\n' + "\n".join(prusa) + "\n</config>\n")
    print("wrote", out)

write("clawd_core2_plate.3mf", [("Clawd body", "clawd_core2_body.stl"), ("Clawd back cover", "clawd_core2_back_cover.stl")])
write("clawd_core2_fit_test.3mf", [("Fit test (front 12 mm)", "fit_test_core2_front_12mm.stl")])
