"""Generate four matching SCons, MSBuild and Rider configurations."""
import argparse
import json
import os
import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
from xml.sax.saxutils import escape

TOOLS = Path(__file__).resolve().parent
SOURCE = TOOLS.parent.parent
WORKSPACE = Path(os.environ.get("OENGINE_WORKSPACE", SOURCE.parent)).resolve()
NS = "http://schemas.microsoft.com/developer/msbuild/2003"
ET.register_namespace("", NS)
variants = json.loads((TOOLS / "EngineConfigurations.json").read_text(encoding="utf-8-sig"))
shell = str(Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe")
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--reuse-metadata", action="store_true", help="Reuse existing per-variant SCons metadata")
args = parser.parse_args()

for index, variant in enumerate(variants):
    variant_metadata = SOURCE / f"godot.{variant['key']}.generated.props"
    if args.reuse_metadata:
        if not variant_metadata.is_file():
            raise FileNotFoundError(f"Generate SCons metadata first: {variant_metadata}")
        continue
    command = [shell, "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
               str(TOOLS / "Godot.ps1"), "-Action", "GenerateMetadata", "-Configuration", variant["name"],
               "-Workspace", str(WORKSPACE)]
    if index == 0:
        command.append("-FullProject")
    print("Generating metadata:", variant["name"], flush=True)
    subprocess.run(command, cwd=SOURCE, check=True)
    metadata = SOURCE / "godot.windows.editor.x86_64.generated.props"
    variant_metadata.write_bytes(metadata.read_bytes())

# SCons currently emits v143 even when it selects a VS 2026 installation. Read
# the installed toolsets from that selected installation instead of hardcoding
# this machine's v145 in the shared template.
metadata_tree = ET.parse(SOURCE / f"godot.{variants[0]['key']}.generated.props")
build_command = metadata_tree.findtext(f".//{{{NS}}}NMakeBuildCommandLine", "")
vcvars = re.search(r'"([^"\n]+[\\/]VC[\\/]Auxiliary[\\/]Build[\\/]vcvars[^"\n]+\.bat)"', build_command, re.IGNORECASE)
toolset = os.environ.get("OENGINE_PLATFORM_TOOLSET")
if not toolset and vcvars:
    installation = Path(vcvars[1]).parents[3]
    installed = [entry for entry in (installation / "MSBuild/Microsoft/VC").glob("v*/Platforms/x64/PlatformToolsets/v[0-9]*")
                 if entry.is_dir() and re.fullmatch(r"v[0-9]+", entry.name)]
    if installed:
        toolset = max(installed, key=lambda entry: int(entry.name[1:])).name
if not toolset or not re.fullmatch(r"v[0-9]+", toolset):
    raise RuntimeError("Set OENGINE_PLATFORM_TOOLSET to an installed MSBuild toolset, e.g. v145")
template = (TOOLS / "godot.vs.user.props.in").read_text(encoding="utf-8-sig")
template = template.replace("@WORKSPACE@", escape(str(WORKSPACE))).replace("@TOOLSET@", toolset)
(SOURCE / "godot.vs.user.props").write_text(template, encoding="utf-8")

project_path = SOURCE / "godot.vcxproj"
tree = ET.parse(project_path)
project = tree.getroot()
for property_node in project.findall(f".//{{{NS}}}PlatformToolset"):
    property_node.text = toolset
configurations = project.find(f"{{{NS}}}ItemGroup[@Label='ProjectConfigurations']")
configurations.clear()
configurations.set("Label", "ProjectConfigurations")
for node in list(project):
    if node.tag == f"{{{NS}}}PropertyGroup" and node.find(f"{{{NS}}}GodotConfiguration") is not None:
        project.remove(node)
    if node.tag == f"{{{NS}}}Import" and ".generated.props" in node.get("Project", ""):
        project.remove(node)
user_import = next(n for n in project if n.tag == f"{{{NS}}}Import" and n.get("Project", "").endswith("godot.vs.user.props"))
insert_at = list(project).index(user_import)
for variant in variants:
    name = variant["name"]
    configuration = ET.SubElement(configurations, f"{{{NS}}}ProjectConfiguration", Include=f"{name}|x64")
    ET.SubElement(configuration, f"{{{NS}}}Configuration").text = name
    ET.SubElement(configuration, f"{{{NS}}}Platform").text = "x64"
    condition = f"'$(Configuration)|$(Platform)'=='{name}|x64'"
    properties = ET.Element(f"{{{NS}}}PropertyGroup", Condition=condition)
    values = {"GodotConfiguration": "editor", "GodotPlatform": "x64",
              "EnginePackageFolder": variant["folder"].replace("/", "\\"), "EngineBinaryName": variant["binary"]}
    for key, value in values.items():
        ET.SubElement(properties, f"{{{NS}}}{key}").text = value
    project.insert(insert_at, properties)
    insert_at += 1
    project.insert(insert_at, ET.Element(f"{{{NS}}}Import",
        Project=f"$(MSBuildProjectDirectory)\\godot.{variant['key']}.generated.props", Condition=condition))
    insert_at += 1
tree.write(project_path, encoding="utf-8", xml_declaration=True)

solution_path = SOURCE / "godot.sln"
solution = solution_path.read_text(encoding="utf-8-sig")
guid = project.find(f"{{{NS}}}PropertyGroup[@Label='Globals']/{{{NS}}}ProjectGuid").text
solution = re.sub(r"(GlobalSection\(SolutionConfigurationPlatforms\) = preSolution\n).*?(\tEndGlobalSection)",
    lambda m: m[1] + "".join(f"\t\t{v['name']}|x64 = {v['name']}|x64\n" for v in variants) + m[2], solution, flags=re.S)
solution = re.sub(r"(GlobalSection\(ProjectConfigurationPlatforms\) = postSolution\n).*?(\tEndGlobalSection)",
    lambda m: m[1] + "".join(f"\t\t{guid}.{v['name']}|x64.{suffix} = {v['name']}|x64\n"
                            for v in variants for suffix in ["ActiveCfg", "Build.0"]) + m[2], solution, flags=re.S)
solution_path.write_text(solution, encoding="utf-8")

run_dir = SOURCE / ".run"
run_dir.mkdir(exist_ok=True)
try:
    package_path = "$PROJECT_DIR$/" + os.path.relpath(WORKSPACE / "Package", SOURCE).replace("\\", "/")
except ValueError:  # A workspace may be on another Windows drive.
    package_path = (WORKSPACE / "Package").as_posix()
for variant in variants:
    name = variant["name"]
    run_path = run_dir / f"{name}.run.xml"
    program_parameters = "--project-manager"
    if run_path.exists():
        old_arguments = ET.parse(run_path).find(".//configuration_1/option[@name='PROGRAM_PARAMETERS']")
        if old_arguments is not None:
            program_parameters = old_arguments.get("value", program_parameters)
    component = ET.Element("component", name="ProjectRunConfigurationManager")
    run = ET.SubElement(component, "configuration", default="false", name=name, type="CppProject", factoryName="C++ Project")
    profile = ET.SubElement(run, "configuration_1", setup="1")
    options = {"CONFIGURATION": name, "PLATFORM": "x64", "CURRENT_LAUNCH_PROFILE": "Local",
               "EXE_PATH": f"{package_path}/{variant['folder']}/{variant['binary']}.exe",
               "PROGRAM_PARAMETERS": program_parameters,
               "WORKING_DIRECTORY": f"{package_path}/{variant['folder']}",
               "PASS_PARENT_ENVS": "1", "USE_EXTERNAL_CONSOLE": "0", "TERMINAL_INTERACTION_BEHAVIOR": "AUTO_DETECT",
               "PROJECT_FILE_PATH": "$PROJECT_DIR$/godot.vcxproj"}
    for key, value in options.items():
        ET.SubElement(profile, "option", name=key, value=value)
    for key in ["DEFAULT_PROJECT_PATH", "PROJECT_FILE_PATH"]:
        ET.SubElement(run, "option", name=key, value="$PROJECT_DIR$/godot.vcxproj")
    ET.SubElement(run, "option", name="AUTO_SELECT_PRIORITY", value="0")
    ET.SubElement(ET.SubElement(run, "method", v="2"), "option", name="Build")
    ET.indent(component)
    ET.ElementTree(component).write(run_path, encoding="utf-8", xml_declaration=True)
print("Rider: Engine, Engine Shipping, Engine Mono, Engine Mono Shipping", flush=True)
