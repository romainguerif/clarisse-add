// Module temoin de ClarisseAdd.
//
// Il ne fait rien. C'est le but : il ne prouve qu'une chose, mais il la prouve
// entierement -- que la chaine CID -> cmagen -> compilation -> edition de liens
// -> chargement par Clarisse fonctionne de bout en bout avec un SDK reconstruit
// a partir de sa seule documentation.
//
// Tant que ce module-la n'apparait pas dans Clarisse, ecrire un vrai filtre
// serait ecrire a l'aveugle.

#include <dso_export.h>
#include <of_app.h>
#include <of_object_factory.h>
#include <module_project_item.h>

// Toujours inclure le .cma genere par cmagen a partir du .cid.
#include <hello.cma>

// Le module : l'implementation C++ derriere chaque objet de la classe. Il ne
// fait rien ici, mais il existe et il est du bon type -- c'est deja tout ce
// qu'on cherche a prouver.
class HelloModule : public ModuleProjectItem {
public:
    HelloModule() : ModuleProjectItem() {}
};

// La doc du SDK ecrit ces deux callbacks avec ModuleObject *. Le vrai typedef,
// lui, dit OfModule * (of_class.h:35-36) -- la doc a pris du retard sur le
// code. Un pointeur de fonction ne tolere pas la covariance du type de retour :
// les signatures doivent coincider exactement.
IX_BEGIN_DECLARE_MODULE_CALLBACKS(AddHello, ModuleObjectCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
IX_END_DECLARE_MODULE_CALLBACKS(AddHello)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(AddHello);
    new_classes.add(new_class);

    IX_MODULE_CLBK *module_callbacks;
    IX_CREATE_MODULE_CLBK(new_class, module_callbacks)
    module_callbacks->cb_create_module = IX_MODULE_CLBK::declare_module;
    module_callbacks->cb_destroy_module = IX_MODULE_CLBK::destroy_module;
}

IX_END_EXTERN_C

OfModule *
IX_MODULE_CLBK::declare_module(OfObject& object, OfObjectFactory& objects)
{
    // Deux choses que la documentation du SDK omet, et sans lesquelles
    // Clarisse plante des le premier objet cree.
    //
    // set_object : OfModule::is_protected() et get_object_name() dereferencent
    // m_object sans le tester (of_module.h:40-41). L'application interroge le
    // module aussitot l'objet ajoute au contexte -- dans
    // AppObjectImpl::on_object_factory_event -- donc sur un m_object nul c'est
    // une violation d'acces, loin de sa cause.
    //
    // set_callbacks, en revanche, est protege (of_module.h) : Clarisse s'en
    // charge lui-meme, ce n'est pas notre affaire.
    //
    // Le module rendu doit aussi correspondre a la classe de base du CID :
    // AddHello derive de ProjectItem, donc HelloModule derive de
    // ModuleProjectItem. Clarisse caste sans verifier.
    HelloModule *module = new HelloModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}
