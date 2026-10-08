using UnrealBuildTool;

public class Bending : ModuleRules
{
	public Bending(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"PhysicsCore",
			"DeveloperSettings",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"EnhancedInput"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"MotionWarping",
			"NetCore"
		});
	}
}
