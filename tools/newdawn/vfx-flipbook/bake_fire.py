# Offline authoring script (private): Mantaflow fire sim -> per-frame RGBA PNGs. Run with
#   blender -b -P bake_fire.py -- <outdir> <res> <samples> <first> <last> [bake=1]
import bpy, sys, os, math, random
argv = sys.argv[sys.argv.index('--') + 1:]
out, res, samples, first, last = argv[0], int(argv[1]), int(argv[2]), int(argv[3]), int(argv[4])
do_bake = len(argv) < 6 or argv[5] == '1'
CW, CH = int(os.environ.get('CW', 192)), int(os.environ.get('CH', 320))
os.makedirs(out, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'CYCLES'
sc.cycles.device = 'CPU'
sc.cycles.samples = samples
sc.cycles.use_denoising = False
sc.render.resolution_x, sc.render.resolution_y = CW, CH
sc.render.film_transparent = True
sc.render.image_settings.file_format = 'PNG'
sc.render.image_settings.color_mode = 'RGBA'
sc.render.image_settings.color_depth = '16'
sc.view_settings.view_transform = 'Standard'
sc.render.fps = 24
sc.frame_start, sc.frame_end = 1, last

# Domain 1.6 x 1.6 x 3.2, base on z=0
bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 0.8))
dom = bpy.context.object
dom.scale = (0.8, 0.8, 1.6)
bpy.ops.object.modifier_add(type='FLUID')
dom.modifiers[-1].fluid_type = 'DOMAIN'
d = dom.modifiers[-1].domain_settings
d.domain_type = 'GAS'
d.resolution_max = res
d.use_noise = True
d.noise_scale = int(os.environ.get('NOISE', 3))
d.noise_strength = float(os.environ.get('NSTR', 1.6))
d.noise_time_anim = 0.8
d.use_dissolve_smoke = True
d.dissolve_speed = 12
d.use_adaptive_domain = False
d.burning_rate = float(os.environ.get('BURN', 1.1))
d.flame_smoke = 0.0
d.flame_vorticity = float(os.environ.get('FVORT', 1.2))
d.vorticity = float(os.environ.get('VORT', 0.35))
d.alpha = 0.6
d.beta = float(os.environ.get('BETA', 1.6))
d.cache_directory = os.path.join(out, 'cache')
d.cache_type = 'ALL'
d.cache_frame_start, d.cache_frame_end = 1, last
d.cache_data_format = 'UNI'

# Torch flame source: a small fuel emitter
bpy.ops.mesh.primitive_uv_sphere_add(radius=float(os.environ.get('EMITR', 0.07)), location=(0, 0, 0.1))
src = bpy.context.object
src.hide_render = True
bpy.ops.object.modifier_add(type='FLUID')
src.modifiers[-1].fluid_type = 'FLOW'
f = src.modifiers[-1].flow_settings
f.flow_type = 'FIRE'
f.flow_behavior = 'INFLOW'
f.flow_source = 'MESH'
f.fuel_amount = float(os.environ.get('FUEL', 0.6))
f.temperature = 1.0
f.surface_distance = 1.5
f.use_plane_init = False
f.subframes = 0

# Flicker: turbulence + gentle wind
bpy.ops.object.effector_add(type='TURBULENCE', location=(0, 0, 0.4))
t = bpy.context.object
t.field.strength = float(os.environ.get('TURB', 1.2))
t.field.size = 0.25
t.field.flow = 0.4
bpy.ops.object.effector_add(type='WIND', location=(0.4, 0, 0.3), rotation=(0, math.radians(90), 0))
w = bpy.context.object
w.field.strength = float(os.environ.get('WIND', 0.8))
w.field.noise = 1.0
w.field.seed = 7

# Material: flame -> emission + density (alpha); small smoke density
mat = bpy.data.materials.new('fire')
mat.use_nodes = True
nt = mat.node_tree
nt.nodes.clear()
vi = nt.nodes.new('ShaderNodeVolumeInfo')
pv = nt.nodes.new('ShaderNodeVolumePrincipled')
outn = nt.nodes.new('ShaderNodeOutputMaterial')
mul = nt.nodes.new('ShaderNodeMath'); mul.operation = 'MULTIPLY'; mul.inputs[1].default_value = float(os.environ.get('FLAMEDENS', 14))
mul2 = nt.nodes.new('ShaderNodeMath'); mul2.operation = 'MULTIPLY'; mul2.inputs[1].default_value = float(os.environ.get('EMIT', 9))
nt.links.new(vi.outputs['Flame'], mul.inputs[0])
nt.links.new(vi.outputs['Flame'], mul2.inputs[0])
if os.environ.get('DEBUGVOL') == 'const':
    pv.inputs['Density'].default_value = 3.0
else:
    nt.links.new(mul.outputs[0], pv.inputs['Density'])
nt.links.new(mul2.outputs[0], pv.inputs['Emission Strength'])
pv.inputs['Color'].default_value = (1, 1, 1, 1)
pv.inputs['Emission Color'].default_value = (1, 1, 1, 1)
nt.links.new(pv.outputs[0], outn.inputs['Volume'])
dom.data.materials.append(mat)

# Orthographic side camera framing the lower 2.1 m: width = CW/CH * height
cam_data = bpy.data.cameras.new('cam'); cam_data.type = 'ORTHO'
H = float(os.environ.get('FRAMEH', 2.4))
cam_data.ortho_scale = H * CH / CH  # blender ortho_scale = larger dimension (height here)
cam = bpy.data.objects.new('cam', cam_data)
sc.collection.objects.link(cam)
cam.location = (0, -6, H / 2 - 0.1)
cam.rotation_euler = (math.radians(90), 0, 0)
sc.camera = cam

if do_bake:
    bpy.context.view_layer.objects.active = dom
    bpy.ops.fluid.bake_all()
for fr in ([] if os.environ.get('NORENDER') else range(first, last + 1)):
    sc.frame_set(fr)
    sc.render.filepath = os.path.join(out, 'f%03d.png' % fr)
    bpy.ops.render.render(write_still=True)
print('DONE', first, last)
