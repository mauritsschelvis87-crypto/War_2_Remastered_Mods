namespace War2ContentStudio.Pud;

public static class PudReader
{
    public static PudDocument ReadFile(string path)
    {
        var bytes = File.ReadAllBytes(path);
        return Read(bytes);
    }

    public static PudDocument Read(byte[] bytes)
    {
        var sections = new List<PudSection>();
        var offset = 0;
        while (offset + 8 <= bytes.Length)
        {
            var name = System.Text.Encoding.ASCII.GetString(bytes, offset, 4);
            var size = BitConverter.ToInt32(bytes, offset + 4);
            offset += 8;
            if (size < 0 || offset + size > bytes.Length)
            {
                throw new InvalidDataException($"Invalid section {name} size {size} at offset {offset - 8}");
            }

            var data = new byte[size];
            if (size > 0)
            {
                Buffer.BlockCopy(bytes, offset, data, 0, size);
            }

            sections.Add(new PudSection { Name = name, Data = data });
            offset += size;
        }

        var doc = new PudDocument { Sections = sections };
        doc.ValidateTypeSection();
        return doc;
    }
}
