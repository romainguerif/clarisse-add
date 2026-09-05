# --- Modifie pour ClarisseAdd -------------------------------------------
# Base sur lookdev-studio_environment.py livre avec Clarisse (c) Isotropix.
# Trois changements par rapport a l'original, tous signales par un commentaire
# "ClarisseAdd" dans le corps du fichier :
#   1. `print x` (Python 2) supprime : il n'apportait rien et inondait la
#      console d'une ligne par fichier du dossier de contenus.
#   2. `project://default` -> `default:/`. Le contexte par defaut a quitte le
#      projet en Clarisse 5 pour devenir sa propre racine (voir la page
#      "New Special Roots" du SDK). L'ancien chemin faisait lever un
#      LookupError des la creation de l'objet d'options. Au passage, le code
#      d'origine concatenait sans slash (`"project://default" + nom`), donc le
#      test d'existence echouait toujours et le script recreait son objet a
#      chaque lancement ; la nouvelle racine se terminant par `/`, la
#      concatenation est correcte telle quelle.
#   3. Le dossier de contenus par defaut peut venir de la variable
#      d'environnement CLARISSE_ADD_LOOKDEV_CONTENT.
#   4. `prefs.is_item_exist(...)` -> `prefs.item_exists(...)`. AppPreferences a
#      renomme la methode en Clarisse 5 ; le script livre avec 5.0 SP14 utilise
#      encore l'ancien nom et leve donc un AttributeError des le lancement.
# -----------------------------------------------------------------------

# This script creates a virtual environment used to simplify lookdev
# from a set of environment projects that can be extended by the user
# To do so, simply create a new project in the content path and
# carefully conform your scene to the original scene hierarchy.
#
# Copyright (C) 2009 - 2019 Isotropix SAS. All rights reserved.
#
# The information in this file is provided for the exclusive use of
# the software licensees of Isotropix. Contents of this file may not
# be distributed, copied or duplicated in any form, in whole or in
# part, without the prior written permission of Isotropix SAS.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

from os import listdir
from os.path import isfile, join

listener = None
lookdev_studio_path = "content_path"
options_name = "lookdev_studio_settings"

def set_lookdev_studio(path):
    ctx = ix.item_exists("project://lookdev_studio")
    if not ctx:
        ctx = ix.create_context("project://lookdev_studio")
    if not ix.item_exists("project://lookdev_studio/environment"):
        ctx.add_context("environment", "Reference")
    env_ref = ix.get_item("project://lookdev_studio/environment")
    attr = env_ref.attribute_exists("filename")
    attr.set_string(path)

def fill_environments(envpath, attr):
    attr.remove_all_presets()
    attr.add_preset("(none)", "")
    attr.set_string(attr.get_preset_value(0))
    if envpath != '':
        files = sorted(listdir(envpath))
        for f in files:
            full_envpath = join(envpath, f)
            if isfile(full_envpath) and f.endswith(".project"):
                attr.add_preset(ix.api.CoreString(f[:-8]).get_title(), full_envpath)


class MyEventListener(ix.api.EventObject):
# This class listens the OfObjectFactory for attribute change
# to perform scene edits accordingly. This can only work as
# the script is runned in modal and the script is persistent.

    def __init__(self, options):
        ix.api.EventObject.__init__(self)
        self.options = options
        # connection to the factory to get notified when attribute change
        self.connect(ix.application.get_factory(), 'EVT_ID_OF_OBJECT_ATTR_CHANGE', self.on_content_path_change)

    def on_content_path_change(self, sender, evtid):
        # as we are connected directly to the factory we can receive
        # event from other objects. We check here if the object sending
        # events is the one we are interested by.
        if self.options == sender.get_last_event_object():
            if self.options.get_changing_attr().get_name() == lookdev_studio_path:
                self.options.attrs.environment.attr.remove_all_presets()
                fill_environments(self.options.get_changing_attr().get_string(), self.options.attrs.environment.attr)
                # Here we to store the content path to the application preferences to avoid
                # the user to reset the content path for each new session of Clarisse
                prefs = ix.application.get_prefs(ix.api.AppPreferences.MODE_APPLICATION)
                if not prefs.item_exists("lookdev_studio", lookdev_studio_path):
                    # we set the pref hidden as when we create the entry it will show up
                    # in Clarisse Preferences Panel. However, upon reload the preference entry
                    # in the config file doesn't store the preference definition so it can't be
                    # displayed properly. So for consistency, we hide it the first time it is created.
                    prefs.add_string("lookdev_studio", lookdev_studio_path, '').set_hidden(True)
                prefs.set_string_value("lookdev_studio", lookdev_studio_path, self.options.get_changing_attr().get_string())
            elif self.options.get_changing_attr().get_name() == "environment":
                set_lookdev_studio(self.options.get_changing_attr().get_string())
            elif self.options.get_changing_attr().get_name() == "enable_shadow_catcher":
                shadow_catcher = ix.item_exists("project://lookdev_studio/environment/IBL/environment")
                if shadow_catcher != None:
                    shadow_catcher.set_disabled(not (self.options.get_changing_attr().get_bool()))
            elif self.options.get_changing_attr().get_name() == "enable_color_checker":
                colorchecker = ix.item_exists("project://lookdev_studio/environment/colorchecker/combiner")
                if colorchecker != None:
                    colorchecker.set_disabled(not (self.options.get_changing_attr().get_bool()))
            elif self.options.get_changing_attr().get_name() == "environment_rotation":
                env_rot = ix.item_exists("project://lookdev_studio/environment/environment_locator")
                if env_rot != None:
                    env_rot.get_attribute("rotate").set_double(self.options.get_changing_attr().get_double(), 1)
            elif self.options.get_changing_attr().get_name() == "sampling_quality":
                render_settings = ix.item_exists("project://lookdev_studio/environment/render_settings.IntegratorPathtracer_embedded")
                ibl = ix.item_exists("project://lookdev_studio/environment/IBL/ibl")
                if render_settings != None and ibl != None:
                    render_settings.get_attribute("material_sample_count").set_long(self.options.get_changing_attr().get_long())
                    ibl.get_attribute("sample_count").set_long(self.options.get_changing_attr().get_long())
                else:
                    ix.log_warning("LookdevStudio: the environment is corrupted!")

# ClarisseAdd : le dossier de contenus vient des preferences si l'utilisateur
# en a deja choisi un, sinon de l'environnement, sinon il reste vide et se
# renseigne dans la fenetre.
import os as _os

envpath = _os.environ.get("CLARISSE_ADD_LOOKDEV_CONTENT", "")
prefs = ix.application.get_prefs(ix.api.AppPreferences.MODE_APPLICATION)
if prefs.item_exists("lookdev_studio", lookdev_studio_path):
    stored = prefs.get_string_value("lookdev_studio", lookdev_studio_path)
    if stored:
        envpath = stored

# check if lookdev studio options object is created in the project
options = ix.item_exists("default:/" + options_name)
if options == None:
    # no options found, we must create it
    ctx = ix.item_exists("project://lookdev_studio")
    if not ctx:
        ctx = ix.create_context("project://lookdev_studio")
    #create options
    options = ix.create_object(options_name, "ProjectItem", ix.get_item("default:/"))
    # the object is hidden as you need this script to run to make scene edits
    options.set_private(True)
    options.set_static(True)

    #add version for version control
    attr = options.add_attribute("version", ix.api.OfAttr.TYPE_LONG, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_FOLDER)
    attr.set_long(1)
    attr.set_hidden(True)

    attr = options.add_attribute(lookdev_studio_path, ix.api.OfAttr.TYPE_STRING, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_FOLDER, "settings")
    attr.set_string(envpath)
    attr = options.add_attribute("environment", ix.api.OfAttr.TYPE_STRING, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_FOLDER,"environment")
    rotate_environment_attr = options.add_attribute("environment_rotation", ix.api.OfAttr.TYPE_DOUBLE, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_ANGLE,"environment")
    shadow_catcher_attr = options.add_attribute("enable_shadow_catcher", ix.api.OfAttr.TYPE_BOOL, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_DEFAULT,"environment")
    shadow_catcher_attr.set_bool(True)
    color_checker_attr = options.add_attribute("enable_color_checker", ix.api.OfAttr.TYPE_BOOL, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_DEFAULT,"environment")
    color_checker_attr.set_bool(True)
    sampling_attr = options.add_attribute("sampling_quality", ix.api.OfAttr.TYPE_LONG, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_SAMPLE_PER_PIXEL,"sampling")
    sampling_attr.set_long(256)
    sampling_attr.set_numeric_range_min(0, True)
    sampling_attr.enable_range(True)
    sampling_attr.set_ui_range_min(0, True)
    sampling_attr.enable_ui_range(True)

    fill_environments(envpath, attr)
    lookdev_studio_ctx = ix.item_exists("project://lookdev_studio/environment")
    # synchronizing the object with the lookdev project as we might have reloaded
    # a project that contained the lookdev studio
    if lookdev_studio_ctx != None:
        lookdev_file_attr = lookdev_studio_ctx.attribute_exists("filename")
        if lookdev_file_attr:
            attr.set_string(lookdev_file_attr.get_string())
            shadow_catcher = ix.item_exists("project://lookdev_studio/environment/IBL/environment")
            if shadow_catcher != None:
                shadow_catcher_attr.set_bool(not shadow_catcher.is_disabled())
            colorchecker = ix.item_exists("project://lookdev_studio/environment/colorchecker/combiner")
            if colorchecker != None:
                color_checker_attr.set_bool(not colorchecker.is_disabled())
            env_rot = ix.item_exists("project://lookdev_studio/environment/environment_locator")
            if env_rot != None:
                rotate_environment_attr.set_double(env_rot.attrs.rotate[1])
            render_settings = ix.item_exists("project://lookdev_studio/environment/render_settings.IntegratorPathtracer_embedded")
            if render_settings != None:
                sampling_attr.set_long(render_settings.get_attribute("material_sample_count").get_long())

else:
    # the option object exists. We make sure to sync its attributes with
    # the current lookdev studio scene state. Indeed the user may have
    # edited the referenced project manually
    env_ref = ix.item_exists("project://lookdev_studio/environment")
    if env_ref != None and env_ref.attribute_exists("filename"):
        options.attrs.environment = env_ref.attribute_exists("filename").get_string()
    else:
        options.attrs.environment = ""
    env_rot = ix.item_exists("project://lookdev_studio/environment/environment_locator")
    if env_rot != None:
        options.attrs.environment_rotation.attr.set_double(env_rot.attrs.rotate[1])
    render_settings = ix.item_exists("project://lookdev_studio/environment/render_settings.IntegratorPathtracer_embedded")
    if render_settings != None:
        options.attrs.sampling_quality.attr.set_long(render_settings.get_attribute("material_sample_count").get_long())
    shadow_catcher = ix.item_exists("project://lookdev_studio/environment/IBL/environment")
    if shadow_catcher != None:
        options.attrs.enable_shadow_catcher[0] = not shadow_catcher.is_disabled()

    colorchecker = ix.item_exists("project://lookdev_studio/environment/colorchecker/combiner")
    if colorchecker != None:
        options.attrs.enable_color_checker[0] = not colorchecker.is_disabled()

listener = MyEventListener(options)
res = ix.application.inspect(options, ix.api.AppDialog.ok(), ix.api.AppDialog.STYLE_OK, "Lookdev Studio")
listener.disconnect_all()
