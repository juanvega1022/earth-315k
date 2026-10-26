using UnrealBuildTool;

public class Earth315kTarget : TargetRules
{
	public Earth315kTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Earth315k");
	}
}
