"""Safely rebuild only the shared unlit celestial material.

Use this instead of the full lunar material generator when changing Earth,
Sun, star or satellite vertex-colour rendering. It deliberately leaves the
terrain and rock materials untouched.
"""

import unreal


PACKAGE_PATH = "/Game/Materials"
OBJECT_PATH = f"{PACKAGE_PATH}/M_LunarCelestial.M_LunarCelestial"

material = (unreal.EditorAssetLibrary.load_asset(OBJECT_PATH)
            if unreal.EditorAssetLibrary.does_asset_exist(OBJECT_PATH) else None)
if material:
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
else:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_LunarCelestial", PACKAGE_PATH, unreal.Material,
        unreal.MaterialFactoryNew()
    )
if not material:
    raise RuntimeError(f"Could not create {OBJECT_PATH}")

material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property("two_sided", True)

def expression(cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(
        material, cls, x, y
    )

tint = expression(unreal.MaterialExpressionVectorParameter, -280, -40)
tint.set_editor_property("parameter_name", "Tint")
tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
vertex = expression(unreal.MaterialExpressionVertexColor, -280, -170)
vertex_tint = expression(unreal.MaterialExpressionMultiply, -70, -80)
unreal.MaterialEditingLibrary.connect_material_expressions(vertex, "", vertex_tint, "A")
unreal.MaterialEditingLibrary.connect_material_expressions(tint, "", vertex_tint, "B")

gain = expression(unreal.MaterialExpressionScalarParameter, -280, 100)
gain.set_editor_property("parameter_name", "ExposureGain")
gain.set_editor_property("default_value", 140.0)
emission = expression(unreal.MaterialExpressionMultiply, -40, 20)
unreal.MaterialEditingLibrary.connect_material_expressions(vertex_tint, "", emission, "A")
unreal.MaterialEditingLibrary.connect_material_expressions(gain, "", emission, "B")
unreal.MaterialEditingLibrary.connect_material_property(
    emission, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
)

unreal.MaterialEditingLibrary.layout_material_expressions(material)
unreal.MaterialEditingLibrary.recompile_material(material)
unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
unreal.log("Updated only M_LunarCelestial: vertex-colour emissive")
