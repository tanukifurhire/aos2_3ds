"""glTF banner scene -> CGFX with skyfloogle/pycgfx (tools/pycgfx, pinned), plus the fixes a real HOME Menu
needs, applied to pycgfx's objects at run time (pycgfx's own files stay untouched). Run by aosbuild:

    python banner_cgfx.py <pycgfx dir> <in.gltf> <out.cgfx> [--autorotate NODE] [--sign 1|-1]
    python banner_cgfx.py <pycgfx dir> <in.gltf> <out.cgfx> --plain --billboard [--layers Logo=0,Ring=2,...]

--plain skips fixes 1-6 (plain pycgfx output, which this HOME menu plays); --billboard makes every node a
Y-axial billboard (BillboardMode.YAxial, as exsilium/3ds-d2): it always faces the camera upright, so the HOME
Menu's spin no longer turns it (a node off the spin axis still circles it). --layers puts the materials whose
names start with each prefix on that render layer (translucency kind: 0 opaque, 1 translucent, 2 subtractive,
3 additive; drawn in that order, whatever the blend): pycgfx's blended materials never write depth, so the
layers fix which quad draws over which.

Fixes found on hardware by daaaaavia/CBC-for-YANBF (MIT) and used by GabeSud0/pokeplatinum-3ds
(tools/banner3d/to_cgfx.py):
  1. every mesh records its node's bone (mesh_node_name);
  2. the bone lookup tree is rebuilt after pycgfx reorders the bones of BLEND materials (otherwise a bone
     lookup finds the wrong bone);
  3. BLEND materials are marked translucent;
  4. materials are unlit (texture x material colour), whatever the HOME Menu's lights do.
And for the HOME Menu's own spin (it turns every banner model about the vertical axis once per 10 s):
  5. the --autorotate node turns the other way at the same rate (+2*pi over the 10 s animation, as tested on
     hardware there), so it and its children stand still;
  6. each curve loops over its own keys inside the 10 s animation.
"""
import importlib.util
import math
import os
import sys

CGFX_LIMIT = 0x80000  # the HOME Menu's limit for a banner's CGFX


def main():
    args = sys.argv[1:]
    opts = {"--autorotate": None, "--sign": "1", "--layers": ""}
    flags = {"--plain": False, "--billboard": False}
    pos = []
    while args:
        a = args.pop(0)
        if a in flags:
            flags[a] = True
        elif a in opts:
            opts[a] = args.pop(0)
        else:
            pos.append(a)
    pycgfx_dir, gltf_path, out_path = pos
    sys.path.insert(0, pycgfx_dir)
    spec = importlib.util.spec_from_file_location("pycgfx_main", os.path.join(pycgfx_dir, "main.py"))
    pycgfx = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pycgfx)

    import gltflib
    from cgfx.canm import (FloatAnimationCurve, FloatSegment, InterpolationType, QuantizationType,
                           RepeatMethod, StepLinear64Key)
    from cgfx.mtob import ConstantColorSource, MTOBFlag

    from cgfx.sobj import BillboardMode

    gltf = gltflib.GLTF.load(gltf_path, load_file_resources=True)
    scene = pycgfx.convert_gltf(gltf)
    cmdl = scene.data.models["COMMON"]
    bones = cmdl.skeleton.bones
    if flags["--billboard"]:
        root = cmdl.skeleton.root_bone
        for name in bones:
            if bones[name] is not root:
                bones[name].billboard_mode = BillboardMode.YAxial
                print(f"billboard: {name}")
    layers = [kv.split("=") for kv in opts["--layers"].split(",") if kv]
    for name in cmdl.materials:
        for prefix, kind in layers:
            if name.startswith(prefix):
                cmdl.materials[name].transluscency_kind = int(kind)
                print(f"layer {kind}: {name}")
    if flags["--plain"]:
        write(pycgfx, scene, out_path)
        return

    # 1. each mesh's node bone (our meshes are named and used by one node each)
    mesh_node = {gltf.model.meshes[n.mesh].name: n.name for n in gltf.model.nodes if n.mesh is not None}
    for sobj_mesh in cmdl.meshes.data.contents:
        mesh_name = next(m for m in mesh_node if sobj_mesh.name.startswith(m + "_"))
        sobj_mesh.mesh_node_name = bones[mesh_node[mesh_name]]
    # 2. the bone lookup tree
    bones.dict.regenerate()
    # 3. translucent BLEND materials, 4. unlit materials
    blend = {m.name for m in (gltf.model.materials or []) if m.alphaMode == "BLEND"}
    for name in cmdl.materials:
        material = cmdl.materials[name]
        if name in blend:
            material.transluscency_kind = 1
        material.flags = MTOBFlag(0)
        stage = material.fragment_shader.texture_combiners[0]
        stage.constant = ConstantColorSource.Diffuse
        stage.src_rgb = stage.src_alpha = 0x0E3
        stage.combine_rgb = stage.combine_alpha = 1
        for stage in material.fragment_shader.texture_combiners[1:]:
            stage.src_rgb = stage.src_alpha = 0x00F
            stage.combine_rgb = stage.combine_alpha = 0
    if list(scene.data.material_animations):
        raise ValueError("material animations are not safe in a HOME Menu banner")
    # 5./6. the animation
    sign = float(opts["--sign"])
    for name in scene.data.skeletal_animations:
        animation = scene.data.skeletal_animations[name]
        animation.looping = True
        for member in animation.member_animations_data:
            bone = animation.member_animations_data[member]
            for curve in vars(bone).values():
                if hasattr(curve, "segments") and curve.segments:
                    curve.start_frame = min(sg.start_frame for sg in curve.segments)
                    curve.end_frame = max(sg.end_frame for sg in curve.segments)
                    curve.pre_repeat_method = curve.post_repeat_method = RepeatMethod.Repeat
            if member == opts["--autorotate"]:
                frames = float(animation.frame_size)
                bone.rot_x = bone.rot_z = None
                bone.scale_x = bone.scale_y = bone.scale_z = None
                bone.pos_x = bone.pos_y = bone.pos_z = None
                curve, seg = FloatAnimationCurve(), FloatSegment()
                curve.start_frame, curve.end_frame = 0.0, frames
                curve.pre_repeat_method = curve.post_repeat_method = RepeatMethod.Repeat
                seg.start_frame, seg.end_frame = 0.0, frames
                seg.interpolation, seg.quantization = InterpolationType.Linear, QuantizationType.StepLinear64
                seg.keys = [StepLinear64Key(0.0, 0.0), StepLinear64Key(frames, sign * 2 * math.pi)]
                curve.segments.append(seg)
                bone.rot_y = curve
                print(f"autorotate: {member}, {sign * 360:+.0f} degrees over {frames / 60:.1f} s")
    write(pycgfx, scene, out_path)


def write(pycgfx, scene, out_path):
    raw = pycgfx.write(scene)
    if len(raw) > CGFX_LIMIT:
        raise ValueError(f"the banner's CGFX is {len(raw)} bytes, over the HOME Menu's {CGFX_LIMIT}")
    with open(out_path, "wb") as f:
        f.write(raw)


if __name__ == "__main__":
    main()
