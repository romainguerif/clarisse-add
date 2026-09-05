# Add current selected geometry to the lookdev studio
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

if not ix.item_exists("project://lookdev_studio/environment/rotate_locator"):
    ix.log_warning("LookdevStudio: You must select a Lookdev Studio Environment first!")
else:
    instance = None
    for obj in ix.selection:
        if obj.is_kindof("Geometry"):
            ctx = ix.get_item("project://lookdev_studio")
            instance = ctx.add_instance(obj)
            instance.attrs.translate.attr.localize(True)
            instance.attrs.rotate.attr.localize(True)
            instance.attrs.shear.attr.localize(True)
            instance.attrs.scale.attr.localize(True)
            instance.attrs.rotation_order.attr.localize(True)
            instance.attrs.parent.attr.localize(True)
            instance.attrs.scale_pivot.attr.localize(True)
            instance.attrs.rotate_pivot.attr.localize(True)
            instance.attrs.translate_offset.attr.localize(True)
            instance.attrs.rotate_offset.attr.localize(True)
            instance.attrs.scale_offset.attr.localize(True)
            instance.attrs.rotation_offset_order.attr.localize(True)
            ix.cmds.CenterObjectsPivots([instance.get_full_name()], True)
            loc = ix.get_item("project://lookdev_studio/environment/rotate_locator")
            instance.attrs.translate[0] = 0
            instance.attrs.translate[1] = 0
            instance.attrs.translate[2] = 0
            instance.attrs.rotate[0] = 0
            instance.attrs.rotate[1] = 0
            instance.attrs.rotate[2] = 0
            instance.attrs.shear[0] = 0
            instance.attrs.shear[1] = 0
            instance.attrs.shear[2] = 0
            instance.attrs.scale[0] = 1
            instance.attrs.scale[1] = 1
            instance.attrs.scale[2] = 1
            instance.get_module().set_parent(loc.get_module(), ix.api.ModuleSceneItem.PARENT_IN_PLACE_FORCED_OFF)

    if not instance:
        ix.log_warning("LookdevStudio: You must select a geometry!")