namespace War2ContentStudio.Editor;

public sealed class EditorSnapshot
{
    public required ushort[] Tiles { get; init; }
    public required List<Pud.PudUnit> Units { get; init; }
    public required string Description { get; init; }
    public required int Era { get; init; }
    public required int MapSize { get; init; }

    public static EditorSnapshot FromDocument(Pud.PudDocument doc) => new()
    {
        Tiles = doc.GetTiles(),
        Units = doc.GetUnits().Select(u => u.Clone()).ToList(),
        Description = doc.Description,
        Era = doc.Era,
        MapSize = doc.MapSize,
    };

    public void ApplyTo(Pud.PudDocument doc)
    {
        doc.Description = Description;
        doc.Era = Era;
        doc.MapSize = MapSize;
        doc.SetTiles(Tiles);
        doc.SetUnits(Units);
    }
}

public sealed class UndoStack<T>
{
    private readonly Stack<T> _undo = new();
    private readonly Stack<T> _redo = new();

    public bool CanUndo => _undo.Count > 0;
    public bool CanRedo => _redo.Count > 0;

    public void Push(T state)
    {
        _undo.Push(state);
        _redo.Clear();
    }

    public T? Undo(T current)
    {
        if (_undo.Count == 0) return default;
        _redo.Push(current);
        return _undo.Pop();
    }

    public T? Redo(T current)
    {
        if (_redo.Count == 0) return default;
        _undo.Push(current);
        return _redo.Pop();
    }
}
