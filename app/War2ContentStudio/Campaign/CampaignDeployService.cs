namespace War2ContentStudio.Campaign;

public sealed class CampaignDeployResult
{
    public int DeployedCount { get; init; }
    public string BackupRoot { get; init; } = string.Empty;
    public List<string> Messages { get; init; } = [];
}

public static class CampaignDeployService
{
    public static CampaignDeployResult Deploy(CampaignProject project)
    {
        if (string.IsNullOrWhiteSpace(project.GameRootPath))
        {
            throw new InvalidOperationException("GameRootPath is required");
        }

        var gameRoot = project.GameRootPath.TrimEnd('\\', '/');
        var campaignDir = Path.Combine(gameRoot, "x86", "Data", "Campaign", project.CampaignFolderName);
        var mapsDir = Path.Combine(gameRoot, "x86", "Maps");
        var backupRoot = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "War2ContentStudio",
            "campaign-backup",
            DateTime.Now.ToString("yyyyMMdd-HHmmss"));

        Directory.CreateDirectory(campaignDir);
        Directory.CreateDirectory(mapsDir);
        Directory.CreateDirectory(backupRoot);

        var messages = new List<string>();
        var count = 0;
        foreach (var mission in project.Missions)
        {
            if (string.IsNullOrWhiteSpace(mission.SourcePudPath) || !File.Exists(mission.SourcePudPath))
            {
                messages.Add($"Skip {mission.Title}: source missing");
                continue;
            }

            var destCampaign = Path.Combine(campaignDir, mission.DeployFileName);
            var destMaps = Path.Combine(mapsDir, mission.DeployFileName);
            BackupIfExists(destCampaign, backupRoot, messages);
            BackupIfExists(destMaps, backupRoot, messages);
            File.Copy(mission.SourcePudPath, destCampaign, overwrite: true);
            File.Copy(mission.SourcePudPath, destMaps, overwrite: true);
            count++;
            messages.Add($"Deployed {mission.Title} -> {destCampaign}");
        }

        return new CampaignDeployResult
        {
            DeployedCount = count,
            BackupRoot = backupRoot,
            Messages = messages,
        };
    }

    private static void BackupIfExists(string path, string backupRoot, List<string> messages)
    {
        if (!File.Exists(path)) return;
        var rel = path.Replace(':', '_');
        var dest = Path.Combine(backupRoot, rel);
        Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
        File.Copy(path, dest, overwrite: true);
        messages.Add($"Backed up {path}");
    }
}
