namespace util {
	template <typename T>
	std::string to_string(const T& value) {
		std::stringstream ss;
		ss << value;
		return ss.str();
	}
}
