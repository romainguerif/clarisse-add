# -*- coding: utf-8 -*-
"""Ce que la licence verrouille reellement, et sous quelle saveur."""
app = ix.application
for probe in ("get_flavor", "get_flavour", "get_license_type"):
    if hasattr(app, probe):
        try:
            print("%-18s : %s" % (probe, getattr(app, probe)()))
        except Exception as e:
            print("%-18s : illisible (%s)" % (probe, e))

factory = app.get_factory().get_classes()
classes = factory.get_classes("")
locked = []
by_family = {}
for i in range(len(classes)):
    cls = classes[i]
    name = cls.get_name()
    try:
        under = cls.is_under_licensed()
    except Exception:
        under = None
    if under:
        try:
            stage = cls.get_release_stage_name(cls.get_release_stage())
        except Exception:
            stage = "?"
        locked.append((name, cls.get_category(), stage))
    by_family.setdefault(cls.get_base_name(), []).append(name)

print("")
print("=" * 68)
print("%-30s %-22s %s" % ("CLASSE VERROUILLEE", "CATEGORIE", "STADE"))
print("-" * 68)
for name, cat, stage in sorted(locked):
    print("%-30s %-22s %s" % (name, cat or "(aucune)", stage))

print("")
print("--- de quoi herite-t-on pour nos modules ? ---")
for base in ("KernelFilter", "ImageFilter", "WholeImageFilter", "Geometry",
             "GeometryPointCloud", "Renderer", "Widget", "Texture", "Process"):
    kids = sorted(by_family.get(base, []))
    print("  %-20s %2d classes derivees : %s"
          % (base, len(kids), ", ".join(kids[:5]) + (" ..." if len(kids) > 5 else "")))
print("=" * 68)
