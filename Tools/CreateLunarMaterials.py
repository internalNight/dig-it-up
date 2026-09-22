import unreal


PACKAGE_PATH = "/Game/Materials"


def make_material(name):
    object_path = f"{PACKAGE_PATH}/{name}.{name}"
    existing = (unreal.EditorAssetLibrary.load_asset(object_path)
                if unreal.EditorAssetLibrary.does_asset_exist(object_path) else None)
    if existing:
        material = existing
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    else:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, PACKAGE_PATH, unreal.Material, unreal.MaterialFactoryNew()
        )
    if not material:
        raise RuntimeError(f"Could not create {object_path}")
    return material


def expression(material, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)


sky = make_material("M_LunarSky")
sky.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
sky.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
sky.set_editor_property("two_sided", True)
black = expression(sky, unreal.MaterialExpressionConstant3Vector, -220, 0)
black.set_editor_property("constant", unreal.LinearColor(0.00005, 0.00005, 0.00008, 1.0))
unreal.MaterialEditingLibrary.connect_material_property(
    black, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
)
unreal.MaterialEditingLibrary.recompile_material(sky)
unreal.EditorAssetLibrary.save_loaded_asset(sky, only_if_is_dirty=False)
unreal.log(f"M_LunarSky shading={sky.get_editor_property('shading_model')} two_sided={sky.get_editor_property('two_sided')}")


celestial = make_material("M_LunarCelestial")
celestial.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
celestial.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
celestial.set_editor_property("two_sided", True)
tint = expression(celestial, unreal.MaterialExpressionVectorParameter, -280, -40)
tint.set_editor_property("parameter_name", "Tint")
tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
celestial_vertex = expression(celestial, unreal.MaterialExpressionVertexColor, -280, -170)
vertex_tint = expression(celestial, unreal.MaterialExpressionMultiply, -70, -80)
unreal.MaterialEditingLibrary.connect_material_expressions(
    celestial_vertex, "", vertex_tint, "A"
)
unreal.MaterialEditingLibrary.connect_material_expressions(
    tint, "", vertex_tint, "B"
)
emission_gain = expression(celestial, unreal.MaterialExpressionScalarParameter, -280, 100)
emission_gain.set_editor_property("parameter_name", "ExposureGain")
emission_gain.set_editor_property("default_value", 140.0)
emission = expression(celestial, unreal.MaterialExpressionMultiply, -40, 20)
unreal.MaterialEditingLibrary.connect_material_expressions(
    vertex_tint, "", emission, "A"
)
unreal.MaterialEditingLibrary.connect_material_expressions(
    emission_gain, "", emission, "B"
)
unreal.MaterialEditingLibrary.connect_material_property(
    emission, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
)
unreal.MaterialEditingLibrary.layout_material_expressions(celestial)
unreal.MaterialEditingLibrary.recompile_material(celestial)
unreal.EditorAssetLibrary.save_loaded_asset(celestial, only_if_is_dirty=False)
unreal.log("Created M_LunarCelestial: unlit, two-sided, vertex-colour emissive")


rock = make_material("M_LunarRock")
rock.set_editor_property("two_sided", False)
rock.set_editor_property("tangent_space_normal", False)
world_position = expression(rock, unreal.MaterialExpressionWorldPosition, -800, -50)
noise = expression(rock, unreal.MaterialExpressionNoise, -590, -50)
noise.set_editor_property("scale", 0.12)
noise.set_editor_property("quality", 2)
noise.set_editor_property("levels", 4)
noise.set_editor_property("level_scale", 2.7)
noise.set_editor_property("output_min", 0.62)
noise.set_editor_property("output_max", 1.15)
noise.set_editor_property("turbulence", True)
unreal.MaterialEditingLibrary.connect_material_expressions(
    world_position, "", noise, "Position"
)
vertex_color = expression(rock, unreal.MaterialExpressionVertexColor, -380, -150)
textured_color = expression(rock, unreal.MaterialExpressionMultiply, -100, -80)
unreal.MaterialEditingLibrary.connect_material_expressions(vertex_color, "", textured_color, "A")
unreal.MaterialEditingLibrary.connect_material_expressions(noise, "", textured_color, "B")
unreal.MaterialEditingLibrary.connect_material_property(
    textured_color, "", unreal.MaterialProperty.MP_BASE_COLOR
)
roughness = expression(rock, unreal.MaterialExpressionConstant, -100, 100)
roughness.set_editor_property("r", 0.96)
unreal.MaterialEditingLibrary.connect_material_property(
    roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
)
specular = expression(rock, unreal.MaterialExpressionConstant, -100, 190)
specular.set_editor_property("r", 0.10)
unreal.MaterialEditingLibrary.connect_material_property(
    specular, "", unreal.MaterialProperty.MP_SPECULAR
)
unreal.MaterialEditingLibrary.layout_material_expressions(rock)
unreal.MaterialEditingLibrary.recompile_material(rock)
unreal.EditorAssetLibrary.save_loaded_asset(rock, only_if_is_dirty=False)


terrain = make_material("M_LunarTerrainMaskV2")
terrain.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
terrain.set_editor_property("opacity_mask_clip_value", 0.5)
terrain.set_editor_property("two_sided", False)
terrain_vertex = expression(terrain, unreal.MaterialExpressionVertexColor, -320, -60)
unreal.MaterialEditingLibrary.connect_material_property(
    terrain_vertex, "", unreal.MaterialProperty.MP_BASE_COLOR
)
unreal.MaterialEditingLibrary.connect_material_property(
    terrain_vertex, "A", unreal.MaterialProperty.MP_OPACITY_MASK
)
terrain_roughness = expression(terrain, unreal.MaterialExpressionConstant, -110, 170)
terrain_roughness.set_editor_property("r", 0.96)
unreal.MaterialEditingLibrary.connect_material_property(
    terrain_roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
)
terrain_specular = expression(terrain, unreal.MaterialExpressionConstant, -110, 250)
terrain_specular.set_editor_property("r", 0.10)
unreal.MaterialEditingLibrary.connect_material_property(
    terrain_specular, "", unreal.MaterialProperty.MP_SPECULAR
)
unreal.MaterialEditingLibrary.layout_material_expressions(terrain)
unreal.MaterialEditingLibrary.recompile_material(terrain)
unreal.EditorAssetLibrary.save_loaded_asset(terrain, only_if_is_dirty=False)

unreal.log("Created lunar sky, celestial, rock and windowed-terrain materials")
