using System.IO;
using UnrealBuildTool;

public class IrritatorCore : ModuleRules
{
    public IrritatorCore(ReadOnlyTargetRules Target) : base(Target)
    {
        // PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PCHUsage = PCHUsageMode.NoSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        // ShadowVariableWarningLevel = WarningLevel.Off;
        CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Off;
        bEnableExceptions = true;

        string ThirdPartyRoot = Path.Combine(ModuleDirectory, "..", "ThirdParty", "irritator");

        string VendorLib = Path.Combine(ModuleDirectory, "Private", "vendor", "irritator-lib");

        if (!Directory.Exists(VendorLib))
        {
            throw new BuildException(
                    "IrritatorCore: '" + VendorLib + "' est introuvable. " +
                    "Launch 'python Source/IrritatorCore/link_vendor.py' after update " +
                    "submodule update of Source/ThirdParty/irritator.");
        }

        PublicIncludePaths.Add(Path.Combine(VendorLib, "include"));
        PublicIncludePaths.Add(Path.Combine(ThirdPartyRoot, "external", "rapidjson", "include"));
        PublicIncludePaths.Add(Path.Combine(ThirdPartyRoot, "external", "fmt", "include"));
        PublicDefinitions.Add("FMT_HEADER_ONLY=1");
        PublicDefinitions.Add("VERSION_MAJOR=0");
        PublicDefinitions.Add("VERSION_MINOR=1");
        PublicDefinitions.Add("VERSION_PATCH=0");
        PublicDefinitions.Add("VERSION_PATCH=0");


        PublicDependencyModuleNames.AddRange(new string[]
                {
                "Core"
                });
    }
}
