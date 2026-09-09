namespace War2ContentStudio.Pud;

public sealed class PudSection
{
    public required string Name { get; init; }
    public required byte[] Data { get; set; }

    public int FourCc => Name.Length == 4
        ? System.Text.Encoding.ASCII.GetBytes(Name)[0]
            | (System.Text.Encoding.ASCII.GetBytes(Name)[1] << 8)
            | (System.Text.Encoding.ASCII.GetBytes(Name)[2] << 16)
            | (System.Text.Encoding.ASCII.GetBytes(Name)[3] << 24)
        : 0;
}
