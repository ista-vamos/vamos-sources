template <typename Value, typename Iterable>
bool __is_in(const Value& v, const Iterable& IT) {
    for (const auto& x : IT) {
        if (x == v)
            return true;
    }
    return false;
}