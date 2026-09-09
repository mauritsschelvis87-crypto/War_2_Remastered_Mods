namespace War2ContentStudio.Pud;

public static class PudWriter
{
    public static byte[] Write(PudDocument document)
    {
        using var ms = new MemoryStream();
        using var bw = new BinaryWriter(ms);
        foreach (var section in document.Sections)
        {
            var nameBytes = System.Text.Encoding.ASCII.GetBytes(section.Name.PadRight(4).Substring(0, 4));
            bw.Write(nameBytes);
            bw.Write(section.Data.Length);
            if (section.Data.Length > 0)
            {
                bw.Write(section.Data);
            }
        }

        return ms.ToArray();
    }

    public static void WriteFile(PudDocument document, string path)
    {
        var dir = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(dir))
        {
            Directory.CreateDirectory(dir);
        }

        File.WriteAllBytes(path, Write(document));
    }
}
