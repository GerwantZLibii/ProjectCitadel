using UnrealBuildTool;

public class ProjectCitadelEditorTarget : TargetRules
{
	public ProjectCitadelEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ProjectCitadel");
	}
}
