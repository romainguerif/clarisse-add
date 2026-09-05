# -*- coding: utf-8 -*-
"""Un premier plan flou devant un fond net : le cas qui trahit le filtre.

Le filtre va chercher, pour chaque pixel d'arrivee, dans un disque dont le
rayon vient de la profondeur de CE pixel. Les pixels de fond qui bordent la
silhouette sont nets, donc leur rayon vaut zero, donc ils sont recopies tels
quels : le premier plan flou garde un bord franc au lieu de se dissoudre sur
le decor. Une vraie optique laisserait voir a travers ce bord sur environ deux
fois le rayon de flou.

La scene est faite pour qu'on puisse MESURER cet ecart, pas pour etre jolie :
deux surfaces planes, frontales, a profondeur constante, l'une claire au fond
et l'autre sombre devant. Chaque plateau est donc parfaitement plat et la
transition entre les deux est une marche verticale nette -- la seule forme sur
laquelle une largeur 10%-90% veut dire quelque chose.

    cnode.exe empty.project -module_path <module> <build> -script <ce fichier>

Ecrit un .project par variante dans OUT, plus un manifeste que measure_edge.py
relit pour savoir ou chercher et a quoi comparer.
"""
import io
import os

OUT = r"J:\_WINDOWSTEMP\claude\fg"
REPORT = r"J:\_WINDOWSTEMP\claude\fg.log"

WIDTH = 640
HEIGHT = 400

# Le rayon maximal du bokeh. 30 px pour que la transition attendue -- environ
# deux fois ce rayon -- ne puisse pas etre confondue avec de l'antialiasing.
RADIUS = 30.0

# Champ horizontal par defaut d'une CameraPerspective, releve sur l'attribut
# field_of_view. Tout le placement en decoule, donc on le recalcule a partir de
# la camera reelle plus bas plutot que de le figer ici.
DEFAULT_FOV = 45.24

CAMERA_Z = 12.0
WALL_Z = -68.0          # le fond, loin derriere le plan de nettete
FRONT_Z = -8.0          # le premier plan, tout pres de l'objectif
FRONT_DEPTH_SIZE = 2.0  # epaisseur de la boite avant

# La zone nette, de part et d'autre de la profondeur du fond. Le fond est un
# mur frontal donc sa profondeur est constante : 4 unites suffisent largement,
# et laissent le premier plan tres au-dela.
FOCUS_RANGE = 4.0

# Les variantes ne different QUE par corrective_slices. "none" = aucun filtre,
# c'est la reference qui donne la largeur d'un bord franc rendu a 4 echantillons.
VARIANTS = [("slices_00_nofilter", "none"), ("slices_01", 1), ("slices_10", 10)]

lines = []


def say(text):
    lines.append(text)
    print("FG| " + text)


def create(name, cls):
    item = ix.cmds.CreateObject(name, cls, "Global", "project:/")
    if item is None:
        say("ECHEC %s (%s)" % (name, cls))
    return item


def setv(item, attribute, *values):
    ix.cmds.SetValues([str(item) + "." + attribute], [str(v) for v in values])


def setvec(item, attribute, *values):
    ix.cmds.SetValues(["%s.%s[%d]" % (str(item), attribute, i)
                       for i in range(len(values))],
                      [str(v) for v in values])


def first_of(item, attribute):
    attr = item.get_attribute(attribute)
    if attr is None or attr.get_value_count() == 0:
        return None
    return attr.get_object(0)


def set_if_present(item, attribute, value, label):
    """Pose une valeur seulement si l'attribut existe deja dans le module.

    corrective_slices est en train d'etre ajoute : tant que la .dll n'est pas
    recompilee, l'attribut est absent et SetValues echouerait en silence. On
    veut que la campagne tourne quand meme, et surtout qu'elle DISE que la
    variante ne mesure rien de particulier aujourd'hui.
    """
    attr = item.get_attribute(attribute)
    if attr is None:
        say("  !! %s : attribut '%s' ABSENT du module charge -- la variante %s"
            % (label, attribute, label))
        say("     est identique aux autres. Recompiler bokeh puis relancer.")
        return False
    setv(item, attribute, value)
    return True


if not os.path.isdir(OUT):
    os.makedirs(OUT)

renderer = create("raytracer", "RendererRaytracer")
setv(renderer, "anti_aliasing_sample_count", 4)

camera = create("camera", "CameraPerspective")
setvec(camera, "translate", 0.0, 0.0, CAMERA_Z)

fov_attr = camera.get_attribute("field_of_view")
fov = fov_attr.get_double(0) if fov_attr is not None else DEFAULT_FOV
say("champ horizontal de la camera : %.4f deg" % fov)

# Demi-largeur du cadre par unite de profondeur. Sert a dimensionner le premier
# plan en PIXELS plutot qu'a l'oeil : le tiers central est une consigne, pas une
# approximation.
import math
half_w = math.tan(math.radians(fov) * 0.5)

wall_depth = CAMERA_Z - WALL_Z
front_depth = CAMERA_Z - (FRONT_Z + FRONT_DEPTH_SIZE * 0.5)

# A la profondeur du premier plan, combien d'unites de scene fait un pixel.
units_per_pixel = (2.0 * half_w * front_depth) / WIDTH
front_width = units_per_pixel * (WIDTH / 3.0)      # le tiers central
front_height = units_per_pixel * (HEIGHT * 0.61)   # de la marge en haut et en bas

say("fond a %.2f unites, premier plan a %.2f" % (wall_depth, front_depth))
say("boite avant : %.3f x %.3f unites, soit %d x %d px"
    % (front_width, front_height, int(WIDTH / 3.0), int(HEIGHT * 0.61)))

# --- materiaux ------------------------------------------------------------
# Deux niveaux francs et surtout PLATS. Les deux surfaces sont frontales donc
# elles ont la meme normale : leur rapport de luminance vaut exactement le
# rapport des albedos, quelle que soit la direction de la lumiere.
mat_wall = create("mat_wall", "MaterialPhysicalDiffuse")
setvec(mat_wall, "front_color", 0.90, 0.90, 0.90)

mat_front = create("mat_front", "MaterialPhysicalDiffuse")
setvec(mat_front, "front_color", 0.14, 0.14, 0.14)

mat_tick = create("mat_tick", "MaterialPhysicalDiffuse")
setvec(mat_tick, "front_color", 0.02, 0.02, 0.02)

# --- le fond --------------------------------------------------------------
wall = create("bg_wall", "GeometryBox")
setvec(wall, "size", 400.0, 400.0, 0.2)
setvec(wall, "translate", 0.0, 0.0, WALL_Z)
setv(wall, "override_material", str(mat_wall))

# Deux bandes de blocs sombres, tres contrastees et a haute frequence : c'est
# ce qui permet de VOIR sur l'image que le fond est bien net. Elles sont
# placees loin de la bande centrale ou se fait la mesure, pour ne jamais
# polluer le plateau de fond dont depend le calcul 10%-90%.
wall_half_height = half_w * wall_depth * HEIGHT / float(WIDTH)
tick_y = wall_half_height * 0.80
tick_w = half_w * wall_depth * 2.0 / 24.0
for row, sign in enumerate((1.0, -1.0)):
    for i in range(12):
        tick = create("bg_tick_%d_%d" % (row, i), "GeometryBox")
        setvec(tick, "size", tick_w, tick_w * 1.5, 0.2)
        setvec(tick, "translate",
               (i - 5.5) * tick_w * 2.0, sign * tick_y, WALL_Z + 0.3)
        setv(tick, "override_material", str(mat_tick))

# --- le premier plan ------------------------------------------------------
front = create("fg_card", "GeometryBox")
setvec(front, "size", front_width, front_height, FRONT_DEPTH_SIZE)
setvec(front, "translate", 0.0, 0.0, FRONT_Z)
setv(front, "override_material", str(mat_front))

# Sans cela, la lumiere inclinee projette l'ombre de la boite avant a des
# centaines de pixels sur le mur : elle traverserait la zone de mesure et
# creuserait un faux plateau de fond.
setv(front, "cast_shadows", 0)

sun = create("sun", "LightPhysicalDistant")
setvec(sun, "rotate", -25.0, 15.0, 0.0)
setv(sun, "intensity", 6.0)

manifest = [u"\t".join(["variant", "image", "project", "radius",
                        "slices_requested", "slices_applied",
                        "bg_depth", "fg_depth", "width", "height"])]

for name, slices in VARIANTS:
    image = create("img_" + name, "Image")

    # resolution_mode 0 = personnalisee. SetValues sur `resolution` est ignore
    # en silence -- l'attribut est pilote par le preset -- alors qu'une
    # ecriture directe passe, comme pour les listes d'AOV plus bas. Sans cela
    # l'image sort en 1920x1080, soit huit fois le temps de rendu demande.
    setv(image, "resolution_mode", 0)
    res = image.get_attribute("resolution")
    res.set_long(WIDTH, 0)
    res.set_long(HEIGHT, 1)
    setv(image, "resolution_multiplier", 1)

    ix.cmds.AddLayer(str(image) + ".layers", "Layer3d")   # renvoie None si ca marche
    layer = first_of(image, "layers")
    setv(layer, "active_camera", str(camera))
    setv(layer, "renderer", str(renderer))
    setv(layer, "output_layer", -1)      # garder tous les canaux, pas seulement RGBA

    # L'AOV de profondeur. Ces deux listes sont `hidden` -- l'editeur d'AOV les
    # peuple d'ordinaire -- mais cache ne veut pas dire en lecture seule.
    selected = layer.get_attribute("selected_aov_list")
    enabled = layer.get_attribute("enabled_aov_list")
    selected.set_value_count(1)
    enabled.set_value_count(1)
    selected.set_string("depth", 0)
    enabled.set_bool(True, 0)

    applied = "n/a"
    if slices != "none":
        layer.get_module().add_filter("ImageFilterBokeh", "bokeh")
        obj = first_of(layer, "filters")

        setv(obj, "radius", RADIUS)
        setv(obj, "depth_aov", "depth")
        setv(obj, "focus_object", "project:/bg_wall")
        setv(obj, "focus_range", FOCUS_RANGE)
        setv(obj, "blur_falloff", 1.0)
        setv(obj, "focus_side", 0)

        # Tout ce qui deformerait le profil mesure est mis a zero : une forme
        # de diaphragme, une frange coloree ou une reprise de hautes lumieres
        # deplacerait les points de passage 10% et 90% pour des raisons qui
        # n'ont rien a voir avec la qualite des bords.
        setv(obj, "blades", 0)
        setv(obj, "softness", 0.0)
        setv(obj, "roundness", 0.0)
        setv(obj, "anamorphism", 0.0)
        setv(obj, "gain", 0.0)
        setv(obj, "optical_vignetting", 0.0)
        setv(obj, "spherical_aberration", 0.0)
        setv(obj, "chromatic_aberration", 0.0)

        ok = set_if_present(obj, "corrective_slices", slices, name)
        applied = str(slices) if ok else "ABSENT"

    project = os.path.join(OUT, name + ".project")
    ix.application.save_project(project)

    manifest.append(u"\t".join([name, "build://project/img_" + name, project,
                                "%.3f" % RADIUS, str(slices), applied,
                                "%.4f" % wall_depth, "%.4f" % front_depth,
                                str(WIDTH), str(HEIGHT)]))
    say("%-20s image=%s  corrective_slices=%s"
        % (name, "build://project/img_" + name, applied))

io.open(os.path.join(OUT, "manifest.tsv"), "w",
        encoding="utf-8").write(u"\n".join(manifest) + u"\n")
io.open(REPORT, "w", encoding="utf-8").write(u"\n".join(lines) + u"\n")
