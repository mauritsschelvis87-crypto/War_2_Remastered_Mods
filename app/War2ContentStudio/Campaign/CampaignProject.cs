namespace War2ContentStudio.Campaign;

public sealed class CampaignMission
{
    public string Id { get; set; } = Guid.NewGuid().ToString("N");
    public string Title { get; set; } = "Mission";
    public string SourcePudPath { get; set; } = string.Empty;
    public string DeployFileName { get; set; } = "mission.pud";
    public string Briefing { get; set; } = string.Empty;
    public int SuggestedRace { get; set; }
}

public sealed class CampaignProject
{
    public int Version { get; set; } = 1;
    public string Name { get; set; } = "Custom Campaign";
    public string CampaignFolderName { get; set; } = "Custom";
    public string GameRootPath { get; set; } = string.Empty;
    public List<CampaignMission> Missions { get; set; } = [];

    public static CampaignProject Load(string path)
    {
        var json = File.ReadAllText(path);
        return System.Text.Json.JsonSerializer.Deserialize<CampaignProject>(json)
            ?? throw new InvalidDataException("Invalid campaign project JSON");
    }

    public void Save(string path)
    {
        var dir = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(dir))
        {
            Directory.CreateDirectory(dir);
        }

        var json = System.Text.Json.JsonSerializer.Serialize(this, new System.Text.Json.JsonSerializerOptions
        {
            WriteIndented = true,
        });
        File.WriteAllText(path, json);
    }
}
