import unreal

def main():
    path = "/Game/Materials/M_GoalFloor"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log("Goal floor already exists; keeping it.")
        return
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_GoalFloor", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
    lib = unreal.MaterialEditingLibrary
    red = lib.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -300, 0)
    red.set_editor_property("constant", unreal.LinearColor(0.7, 0.006, 0.012, 1))
    lib.connect_material_property(red, "", unreal.MaterialProperty.MP_BASE_COLOR)
    glow = lib.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -300, 150)
    glow.set_editor_property("constant", unreal.LinearColor(0.22, 0.002, 0.004, 1))
    lib.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -300, 300)
    rough.set_editor_property("r", 0.8)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)

main()
