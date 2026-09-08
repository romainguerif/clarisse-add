# -*- coding: utf-8 -*-
"""Le tube produit-il vraiment un maillage ?

Quatre locators, un GeometryTube branche dessus, et on regarde ce qui sort :
nombre de sommets, nombre de primitives, boite englobante, UV. Puis on deplace
un locator pour verifier que la geometrie se reconstruit -- c'est le seul point
que la documentation du SDK ne permettait pas de trancher a l'avance.

Lance par cnode via -script.
"""
CLASS = "GeometryTube"

print("")
print("=" * 64)

classes = ix.application.get_factory().get_classes()
found = classes.exists(CLASS)
print("classe '%s' declaree : %s" % (CLASS, "OUI" if found else "NON"))

if found:
    cls = classes.get(CLASS)
    print("declaree par          : %s" % cls.get_dso_filename())
    print("classe de base        : %s" % cls.get_base_name())

if not found:
    print("=" * 64)
else:
    # Quatre points en L, pour que la courbe tourne : un tube parfaitement
    # droit ne dirait rien du repere ni de l'interpolation.
    positions = [(0.0, 0.0, 0.0), (2.0, 0.0, 0.0), (4.0, 0.0, 2.0), (4.0, 2.0, 4.0)]
    locators = []
    for i, p in enumerate(positions):
        loc = ix.cmds.CreateObject("cp%d" % i, "Locator", "Global", "project:/")
        ix.cmds.SetValues([str(loc) + ".translate"],
                          [str(p[0]), str(p[1]), str(p[2])])
        locators.append(loc)
    print("locators crees        : %d" % len(locators))
    print("nom complet du 1er    : %s" % str(locators[0]))
    print("sa position relue     : %s"
          % [locators[0].get_attribute("translate").get_double(k) for k in range(3)])

    tube = ix.cmds.CreateObject("tube", CLASS, "Global", "project:/")
    print("instanciation         : %s" % ("OUI" if tube is not None else "NON"))

    module = tube.get_module()
    print("module C++ attache    : %s" % ("OUI" if module is not None else "NON"))

    attr = tube.get_attribute("control_points")
    print("attribut points       : %s" % ("absent" if attr is None else "present"))

    if attr is not None:
        print("type de l'attribut    : %s" % attr.get_type())
        try:
            ix.cmds.AddValues([str(tube) + ".control_points"],
                              [str(loc) for loc in locators])
        except Exception as error:
            print("AddValues a echoue    : %s" % error)
        if attr.get_value_count() == 0:
            # Repli direct par l'API, sans passer par la couche de commandes.
            try:
                attr.set_value_count(len(locators))
                for i, loc in enumerate(locators):
                    attr.set_object(loc, i)
                print("branche par set_object : oui")
            except Exception as error:
                print("set_object a echoue   : %s" % error)
        print("points branches       : %d" % attr.get_value_count())

    def look(label):
        """Ce que le module expose une fois la geometrie construite."""
        try:
            mesh = module.get_geometry()
            print("%-21s : %d sommets, %d polygones"
                  % (label, mesh.get_vertex_count(), mesh.get_polygon_count()))
        except Exception as error:
            print("%-21s : maillage illisible (%s)" % (label, error))
        try:
            bbox = module.get_bbox()
            lo = bbox.get_min()
            hi = bbox.get_max()
            print("%-21s : [%.2f %.2f %.2f] -> [%.2f %.2f %.2f]"
                  % (label, lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]))
        except Exception as error:
            print("%-21s : illisible (%s)" % (label, error))

    geo = None
    for name in ("get_geometry", "get_surface", "get_mesh"):
        if not hasattr(module, name):
            continue
        try:
            geo = getattr(module, name)()
            if geo is not None:
                print("geometrie via         : %s -> %s"
                      % (name, geo.get_class_info_name()))
                break
        except Exception as error:
            print("%s a echoue : %s" % (name, error))

    if geo is not None:
        for probe in ("get_vertex_count", "get_polygon_count",
                      "get_primitive_count", "get_uv_map_count"):
            if hasattr(geo, probe):
                try:
                    print("%-21s : %s" % (probe[4:], getattr(geo, probe)()))
                except Exception as error:
                    print("%-21s : illisible (%s)" % (probe[4:], error))

    look("etat initial")

    # Le point non tranche par la doc : est-ce que bouger un point de controle
    # invalide bien la ressource et fait rappeler create_resource ?
    ix.cmds.SetValues([str(locators[3]) + ".translate"], ["4.0", "8.0", "4.0"])
    look("apres deplacement")

    # Et le rayon, qui passe par un autre chemin d'attribut.
    ix.cmds.SetValues([str(tube) + ".radius"], ["0.5"])
    look("apres rayon 0.5")

    ix.cmds.SetValues([str(tube) + ".sides"], ["6"])
    look("apres sides 6")

    print("=" * 64)
