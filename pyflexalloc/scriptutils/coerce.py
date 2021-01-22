

def to_bool(v):
    """coerce value to boolean"""
    if isinstance(v, bool):
        return v
    v = str(v).lower().strip()
    return v in {"1", "true", "y", "yes"}
