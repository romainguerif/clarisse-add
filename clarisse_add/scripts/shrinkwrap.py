# Auto Shrink Wrap
# Select the objects you want to "shrink wrap" and run the script
# You will find the "shrink wrapped" plane in the "project://Shrink_Plane/" context
# You can tweak the size and the translate to focus an area. Tweak spans for more or less detail

sel = ix.selection
count = sel.get_count()

shrinkContext = ix.cmds.CreateContext("ShrinkWrap", "project:/")

cbn = ix.cmds.CreateObject("CBN_", "SceneObjectCombiner", str(shrinkContext))
grp = ix.cmds.CreateObject("Occluders", "Group", str(shrinkContext))

if count > 0:
    for i in range(count):
        obj = sel[i].get_full_name()
        ix.cmds.AddValues([str(cbn) + ".objects"], [obj])
        ix.cmds.AddValues([str(grp) + ".inclusion_items"], [obj])


bounding_box = cbn.get_module().get_bbox()
bbVal_min = bounding_box[0]
bbVal_max = bounding_box[1]

xmin = bbVal_min[0]
ymin = bbVal_min[1]
zmin = bbVal_min[2]

xmax = bbVal_max[0]
ymax = bbVal_max[1]
zmax = bbVal_max[2]

xCenter = ((xmin + xmax) * 0.5)
yCenter = ((ymin + ymax) * 0.5)
zCenter = ((zmin + zmax) * 0.5)

bbWidth = (xmax - xmin)
bbHeight = (ymax - ymin)
bbDepth = (zmax - zmin)

spanX = 300
spanZ = 300

shrinkPlane = ix.cmds.CreateObject("Shrink_Plane", "GeometryPolygrid", str(shrinkContext))
ix.cmds.SetValues([str(shrinkPlane) + ".translate"], [str(xCenter), str(yCenter + bbHeight), str(zCenter)])
ix.cmds.SetValues([str(shrinkPlane) + ".spans[0]"], [str(spanX)])
ix.cmds.SetValues([str(shrinkPlane) + ".spans[1]"], [str(spanZ)])


ix.cmds.SetValues([str(shrinkPlane) + ".size[0]"], [str(bbWidth - 0.001)])
ix.cmds.SetValues([str(shrinkPlane) + ".size[1]"], [str(bbDepth - 0.001)])

occTex = ix.cmds.CreateObject("occlusion", "TextureOcclusion", str(shrinkContext))

ix.cmds.SetCurveKeyType([str(occTex) + ".distance_falloff"], [0, 1, 0, 0, 2, 0])
ix.cmds.SetValues([str(occTex) + ".occlusion_mode"], ["2"])
ix.cmds.SetValues([str(occTex) + ".angle"], ["0.0"])
ix.cmds.SetValues([str(occTex) + ".quality"], ["1"])
ix.cmds.SetValues([str(occTex) + ".radius"], [str(bbHeight * 1.5)])
ix.cmds.SetValues([str(occTex) + ".color"], [str(bbHeight * -1.5), str(bbHeight * -1.5), str(bbHeight * -1.5)])
ix.cmds.SetValues([str(occTex) + ".occluders"], [str(grp)])

deformer = ix.cmds.AddValues([str(shrinkPlane) + ".deformers"], ["DeformerDisplacement"])
ix.cmds.SetValues([str(shrinkPlane) + ".displacement.texture"], [str(occTex)])
ix.cmds.SetValues([str(shrinkPlane) + ".displacement.local_deformation"], ["0"])
ix.cmds.SetValues([str(shrinkPlane) + ".displacement.displacement_axis"], ["1"])

ix.cmds.CenterObjectsPivots([str(shrinkPlane)], False)

##################################################################################
##################################################################################
##################################################################################

bakingContext = ix.cmds.CreateContext("Baking", shrinkContext)

material = ix.cmds.CreateObject("bake_mat", "MaterialMatte", "Global", bakingContext)
rescale = ix.cmds.CreateObject("rescale", "TextureRescale", "Global", bakingContext)
multiply = ix.cmds.CreateObject("multiply", "TextureMultiply", "Global", bakingContext)

ix.cmds.SetTexture([str(material) + ".color"], str(rescale))
ix.cmds.SetTexture([str(rescale) + ".input"], str(multiply))
ix.cmds.SetTexture([str(multiply) + ".input1"], str(occTex))

ix.cmds.SetValues([str(multiply) + ".input2"], ["-1", "-1", "-1"])

ix.cmds.SetValues([str(rescale) + ".output_min"], ["1", "1", "1"])
ix.cmds.SetValues([str(rescale) + ".output_max"], ["0", "0", "0"])
ix.cmds.SetValues([str(rescale) + ".input_max"], [str(bbHeight * 1.5), str(bbHeight * 1.5), str(bbHeight * 1.5)])

bakePlane = ix.cmds.CreateObject("Bake_Plane", "GeometryPolygrid", "Global", bakingContext)
ix.cmds.SetValues([str(bakePlane) + ".translate"], [str(xCenter), str(yCenter + bbHeight), str(zCenter)])
ix.cmds.SetValues([str(bakePlane) + ".spans[0]"], [str(spanX)])
ix.cmds.SetValues([str(bakePlane) + ".spans[1]"], [str(spanZ)])
ix.cmds.SetValues([str(bakePlane) + ".size[0]"], [str(bbWidth - 0.001)])
ix.cmds.SetValues([str(bakePlane) + ".size[1]"], [str(bbDepth - 0.001)])
ix.cmds.SetValues([str(bakePlane) + ".materials[0]"], [str(material)])

raytracer = ix.cmds.CreateObject("raytracer", "RendererRaytracer", "Global", bakingContext)
ix.cmds.SetValues([str(raytracer) + ".anti_aliasing_sample_count"], ["6"])
ix.cmds.SetValues([str(raytracer) + ".anti_aliasing_filter"], ["3"])

cam = ix.cmds.CreateObject("perspective", "CameraPerspective", "Global", bakingContext)

image = ix.cmds.CreateObject("Baked_Render", "Image", "Global", bakingContext)
ix.cmds.SetValues([str(image) + ".resolution[0]"], ["1024"])
ix.cmds.SetValues([str(image) + ".resolution[1]"], ["1024"])
ix.cmds.SetValues([str(image) + ".resolution_multiplier"], ["2"])

layer = ix.cmds.AddLayer(str(image) + ".layers", "Layer3d")
ix.cmds.SetValues([str(image) + ".layer_3d.enable_uv_bake"], ["1"])
ix.cmds.SetValues([str(image) + ".layer_3d.uv_bake_geometry"], [bakePlane])
ix.cmds.SetValues([str(image) + ".layer_3d.renderer"], [raytracer])
ix.cmds.SetValues([str(image) + ".layer_3d.active_camera"], [cam])

ix.selection.select(ix.get_item(str(shrinkContext)))