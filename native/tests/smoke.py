# -*- coding: utf-8 -*-
"""Le module temoin est-il charge ? Repond par oui ou par non, sans nuance.

Lance par cnode via -script, apres le chargement des modules.
"""
CLASS = "AddHello"

print("")
print("=" * 60)

classes = ix.application.get_factory().get_classes()
found = classes.exists(CLASS)
print("classe '%s' declaree : %s" % (CLASS, "OUI" if found else "NON"))

if found:
    # Ce que le CID a reellement transmis, lu sur le prototype de la classe :
    # aucune instanciation, donc aucun risque de planter avant d'avoir parle.
    cls = classes.get(CLASS)
    proto = cls.get_proto()
    # get_dso_filename dit de quelle bibliotheque vient la classe : c'est la
    # preuve que le .dll a bien ete charge, et pas qu'un homonyme existe.
    print("declaree par          : %s" % cls.get_dso_filename())
    print("classe de base        : %s" % cls.get_base_name())
    attr = proto.get_attribute("message")
    print("attribut 'message'    : %s" % ("absent" if attr is None
                                          else '"%s"' % attr.get_string()))

if not found:
    print("=" * 60)
else:
    # Declaree ne suffit pas : il faut qu'elle s'instancie, que son module se
    # cree, et que l'attribut venu du CID soit la avec sa valeur par defaut.
    # Temoin de controle : si meme un Locator natif ne se cree pas ici, le
    # probleme n'est pas dans notre module mais dans cette facon d'appeler.
    native = ix.cmds.CreateObject("controle", "Locator", "Global", "project:/")
    print("Locator natif cree    : %s" % ("OUI" if native is not None else "NON"))

    item = ix.cmds.CreateObject("temoin", CLASS, "Global", "project:/")
    print("instanciation         : %s" % ("OUI" if item is not None else "NON"))
    if item is not None:
        print("module C++ attache    : %s"
              % ("OUI" if item.get_module() is not None else "NON"))
    print("=" * 60)
