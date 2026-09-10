import unreal


PACKAGE_PATH = "/Game/Materials"
ASSET_NAME = "M_RegolithFine"
OBJECT_PATH = f"{PACKAGE_PATH}/{ASSET_NAME}.{ASSET_NAME}"


def expression(material, expression_class, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(
        material, expression_class, x, y
    )


if unreal.EditorAssetLibrary.does_asset_exist(OBJECT_PATH):
    raise RuntimeError(
        f"{OBJECT_PATH} already exists. Reuse it or choose a new ASSET_NAME; "
        "do not destroy expressions of a material referenced by a live actor CDO."
    )
material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    ASSET_NAME, PACKAGE_PATH, unreal.Material, unreal.MaterialFactoryNew()
)

if material is None:
    raise RuntimeError(f"Could not create {OBJECT_PATH}")

unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
material.set_editor_property("two_sided", False)
material.set_editor_property("tangent_space_normal", False)

world_position = expression(material, unreal.MaterialExpressionWorldPosition, -900, -50)

noise = expression(material, unreal.MaterialExpressionNoise, -650, -50)
noise.set_editor_property("scale", 3.2)
noise.set_editor_property("quality", 1)
noise.set_editor_property("levels", 2)
noise.set_editor_property("level_scale", 2.4)
noise.set_editor_property("output_min", 0.0)
noise.set_editor_property("output_max", 1.0)
noise.set_editor_property("turbulence", True)
unreal.MaterialEditingLibrary.connect_material_expressions(
    world_position, "", noise, "Position"
)

dark_sand = expression(material, unreal.MaterialExpressionConstant3Vector, -420, -180)
dark_sand.set_editor_property("constant", unreal.LinearColor(0.32, 0.335, 0.35, 1.0))
light_sand = expression(material, unreal.MaterialExpressionConstant3Vector, -420, -80)
light_sand.set_editor_property("constant", unreal.LinearColor(0.46, 0.475, 0.49, 1.0))

color_lerp = expression(material, unreal.MaterialExpressionLinearInterpolate, -150, -100)
unreal.MaterialEditingLibrary.connect_material_expressions(dark_sand, "", color_lerp, "A")
unreal.MaterialEditingLibrary.connect_material_expressions(light_sand, "", color_lerp, "B")
unreal.MaterialEditingLibrary.connect_material_expressions(noise, "", color_lerp, "Alpha")
macro_noise = expression(material, unreal.MaterialExpressionNoise, -650, -400)
macro_noise.set_editor_property("scale", 0.012)
macro_noise.set_editor_property("quality", 1)
macro_noise.set_editor_property("levels", 2)
macro_noise.set_editor_property("output_min", 0.93)
macro_noise.set_editor_property("output_max", 1.03)
unreal.MaterialEditingLibrary.connect_material_expressions(world_position, "", macro_noise, "Position")
base_color = expression(material, unreal.MaterialExpressionMultiply, 100, -100)
unreal.MaterialEditingLibrary.connect_material_expressions(color_lerp, "", base_color, "A")
unreal.MaterialEditingLibrary.connect_material_expressions(macro_noise, "", base_color, "B")
unreal.MaterialEditingLibrary.connect_material_property(base_color, "", unreal.MaterialProperty.MP_BASE_COLOR)

# Screen derivatives give filtered fine-grain shading without adding mesh vertices.
normal_ws = expression(material, unreal.MaterialExpressionVertexNormalWS, -420, -550)
bump = expression(material, unreal.MaterialExpressionCustom, 100, -400)
bump.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
custom_inputs = []
for name in ("P", "N", "H"):
    custom_input = unreal.CustomInput()
    custom_input.set_editor_property("input_name", name)
    custom_inputs.append(custom_input)
bump.set_editor_property("inputs", custom_inputs)
bump.set_editor_property("code", """
float3 dx = ddx(P), dy = ddy(P);
float3 rx = cross(dy, N), ry = cross(N, dx);
float det = dot(dx, rx);
float3 grad = (ddx(H) * rx + ddy(H) * ry) / max(abs(det), 1e-5) * sign(det);
return normalize(N - 0.02 * grad);
""")
unreal.MaterialEditingLibrary.connect_material_expressions(world_position, "", bump, "P")
unreal.MaterialEditingLibrary.connect_material_expressions(normal_ws, "", bump, "N")
unreal.MaterialEditingLibrary.connect_material_expressions(noise, "", bump, "H")
unreal.MaterialEditingLibrary.connect_material_property(bump, "", unreal.MaterialProperty.MP_NORMAL)

rough_low = expression(material, unreal.MaterialExpressionConstant, -400, 100)
rough_low.set_editor_property("r", 0.80)
rough_high = expression(material, unreal.MaterialExpressionConstant, -400, 180)
rough_high.set_editor_property("r", 0.96)
rough_lerp = expression(material, unreal.MaterialExpressionLinearInterpolate, -150, 130)
unreal.MaterialEditingLibrary.connect_material_expressions(rough_low, "", rough_lerp, "A")
unreal.MaterialEditingLibrary.connect_material_expressions(rough_high, "", rough_lerp, "B")
unreal.MaterialEditingLibrary.connect_material_expressions(noise, "", rough_lerp, "Alpha")
unreal.MaterialEditingLibrary.connect_material_property(
    rough_lerp, "", unreal.MaterialProperty.MP_ROUGHNESS
)

specular = expression(material, unreal.MaterialExpressionConstant, -150, 260)
specular.set_editor_property("r", 0.18)
unreal.MaterialEditingLibrary.connect_material_property(
    specular, "", unreal.MaterialProperty.MP_SPECULAR
)

unreal.MaterialEditingLibrary.layout_material_expressions(material)
unreal.MaterialEditingLibrary.recompile_material(material)
unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
unreal.log(f"Created runtime sand material: {OBJECT_PATH}")
