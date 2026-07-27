using System.Globalization;

namespace DragonPixel.Contracts;

/// <summary>A language-neutral persistent Dragon Pixel identifier.</summary>
public readonly struct DpeId : IEquatable<DpeId>, IComparable<DpeId>
{
    private readonly Guid _value;

    public DpeId(Guid value)
    {
        if (value == Guid.Empty)
        {
            throw new ArgumentException("Dragon Pixel identifiers cannot be empty.", nameof(value));
        }
        _value = value;
    }

    public static DpeId New() => new(Guid.NewGuid());

    public static bool TryParse(string? value, out DpeId result)
    {
        if (Guid.TryParseExact(value, "D", out var parsed) && parsed != Guid.Empty)
        {
            result = new DpeId(parsed);
            return true;
        }
        result = default;
        return false;
    }

    public Guid ToGuid() => _value;
    public int CompareTo(DpeId other) => _value.CompareTo(other._value);
    public bool Equals(DpeId other) => _value.Equals(other._value);
    public override bool Equals(object? obj) => obj is DpeId other && Equals(other);
    public override int GetHashCode() => _value.GetHashCode();
    public override string ToString() => _value.ToString("D", CultureInfo.InvariantCulture).ToLowerInvariant();
    public static bool operator ==(DpeId left, DpeId right) => left.Equals(right);
    public static bool operator !=(DpeId left, DpeId right) => !left.Equals(right);
}
