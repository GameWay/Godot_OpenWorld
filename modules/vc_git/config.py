def can_build(env, platform):
    # An editor plugin written in GDScript (see addon/), so editor builds with GDScript only.
    env.module_add_dependencies("vc_git", ["gdscript"])
    return env.editor_build


def configure(env):
    pass
