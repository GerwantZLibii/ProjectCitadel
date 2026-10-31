using UnrealBuildTool;

public class ProjectCitadelTarget : TargetRules
{
	public ProjectCitadelTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ProjectCitadel");
	}
}
