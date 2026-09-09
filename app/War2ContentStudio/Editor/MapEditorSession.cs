namespace War2ContentStudio.Editor;

public enum EditorTool
{
    Select,
    Terrain,
    Unit,
    Eraser,
}

public sealed class MapEditorSession
{
    public Pud.PudDocument Document { get; private set; }
    public string? FilePath { get; set; }
    public UndoStack<EditorSnapshot> History { get; } = new();
    public EditorTool ActiveTool { get; set; } = EditorTool.Terrain;
    public ushort TerrainTile { get; set; } = 0x0050;
    public byte UnitType { get; set; } = 0x00;
    public byte UnitOwner { get; set; }
    public double Zoom { get; set; } = 4;

    public MapEditorSession(Pud.PudDocument document)
    {
        Document = document;
    }

    public void CommitSnapshot()
    {
        History.Push(EditorSnapshot.FromDocument(Document));
    }

    public void Undo()
    {
        var current = EditorSnapshot.FromDocument(Document);
        var prev = History.Undo(current);
        if (prev is null) return;
        prev.ApplyTo(Document);
    }

    public void Redo()
    {
        var current = EditorSnapshot.FromDocument(Document);
        var next = History.Redo(current);
        if (next is null) return;
        next.ApplyTo(Document);
    }

    public void Load(Pud.PudDocument document, string? path)
    {
        Document = document;
        FilePath = path;
        History.Push(EditorSnapshot.FromDocument(document));
    }

    public void PaintTile(int x, int y)
    {
        var size = Document.MapSize;
        if (x < 0 || y < 0 || x >= size || y >= size) return;
        var tiles = Document.GetTiles();
        var idx = y * size + x;
        if (tiles[idx] == TerrainTile) return;
        CommitSnapshot();
        tiles[idx] = TerrainTile;
        Document.SetTiles(tiles);
    }

    public void PlaceUnit(int x, int y)
    {
        var size = Document.MapSize;
        if (x < 0 || y < 0 || x >= size || y >= size) return;
        CommitSnapshot();
        var units = Document.GetUnits();
        units.Add(new Pud.PudUnit
        {
            X = (ushort)x,
            Y = (ushort)y,
            Type = UnitType,
            Owner = UnitOwner,
            Ai = 0,
        });
        Document.SetUnits(units);
    }

    public void EraseAt(int x, int y)
    {
        var size = Document.MapSize;
        if (x < 0 || y < 0 || x >= size || y >= size) return;
        var units = Document.GetUnits();
        var before = units.Count;
        units.RemoveAll(u => u.X == x && u.Y == y);
        if (units.Count == before) return;
        CommitSnapshot();
        Document.SetUnits(units);
    }
}
