#pragma once

#include <variant>
#include <string>
#include <unordered_map>
#include <sstream>

#include <algorithm>

namespace hpjson
{

struct json_type
{
	enum
	{
		array,
		object,

		null,
		num_double,
		num_i32,
		boolean,
		string,

		num_float, num_ui32, num_i64, num_ui64
	};
};

constexpr const char* const json_type_name_of(size_t x)
{
	switch (x)
	{
	case json_type::array:return "array";
	case json_type::object:return "object";

	case json_type::null:return "null";
	case json_type::num_double:return "number.double";
	case json_type::num_i32:return "number.int32";
	case json_type::boolean:return "bool";
	case json_type::string:return "string";

	case json_type::num_float:return "number.float";
	case json_type::num_ui32:return "number.uint32";
	case json_type::num_i64:return "number.int64";
	case json_type::num_ui64:return "number.uint64";

	}
	return "unknown";
}

class json_pointer;

template<
	typename _string_t = std::string,
	template <typename _key_t, typename _val_t> typename _map_t = std::unordered_map
>
class _basic_json
{
private:

	class _my_initializer_list;

public:

	using string_t = _string_t;
	using string_char_t = typename string_t::value_type;

	class object : public _map_t<string_t, _basic_json>
	{
	public:
	};
	class array : public std::vector<_basic_json>
	{
	public:
		using _base_t = std::vector<_basic_json>;
		array() : _base_t() {}
		array(std::initializer_list<_my_initializer_list> x)
		{
			for (auto& it : x)
				this->push_back(it.data());
		}
	};

private:

	std::variant <
		array, object,

		nullptr_t, double, int, bool, string_t,

		float, uint32_t, int64_t, uint64_t
	> _data;

	template<typename _t>
	using _assign_enable_if_t = typename std::enable_if_t<
		!std::is_same_v<_t, decltype(_data)> &&
		std::is_constructible_v<decltype(_data), _t>,
	int>;

public:

	_basic_json() :_data(nullptr) {}
	template <typename _t>
	_basic_json(const _t& x) { assign(x); }
	_basic_json(const std::initializer_list<_my_initializer_list>& x)
	{
		_assign(_my_initializer_list(x));
	}

	inline bool is_val()const noexcept { return type() > json_type::object; }
	inline bool is_arr()const noexcept { return is<array>(); }
	inline bool is_obj()const noexcept { return is<object>(); }

	inline size_t type()const noexcept { return _data.index(); }
	const char* const type_name()const noexcept { return json_type_name_of(type()); }

	inline void clear() { _data = nullptr; }

	// assign

	template <typename _t, _assign_enable_if_t<_t> = 0>
	void assign(const _t& x) { _data = x; }
	// char [] 会被当成 bool 是为什么?
	void assign(const string_char_t x[]) { _data = std::move(string_t(x)); }
	void assign(const _basic_json& x) { _data = x._data; }

	const _basic_json& operator=(const _basic_json& x)
	{
		assign(x);
		return x;
	}
	const auto& operator=(std::initializer_list<_my_initializer_list>& x)
	{
		_assign(_my_initializer_list(x));
		return x;
	}

	// operator _t

	inline operator const array& () const { return get<array>(); }
	inline operator const object& () const { return get<object>(); }
	template <
		typename _t,
		typename std::enable_if_t<
		!(
			// 消除 string 歧义
			std::is_pointer_v<_t> // [const] char* [const]
			|| std::is_same_v<_t, string_char_t>
			|| std::is_same_v<_t, std::initializer_list<string_char_t> >
			), int
		> = 0
	> inline operator _t () const { return get<_t>(); }

	// get<_t>

	template<typename _t>
	_t& get()
	{
		_ensure_is<_t>();
		return std::get<_t>(_data);
	}
	template<typename _t>
	const _t& get()const
	{
		return _ensure_is<_t>() ? std::get<_t>(_data) : _make_tmp<_t>();
	}

	// get<_idx>

	template<size_t _idx>
	inline auto& get()
	{
		return get<std::variant_alternative_t<_idx, decltype(_data)> >();
	}
	template<size_t _idx>
	inline const auto& get()const
	{
		return get<std::variant_alternative_t<_idx, decltype(_data)> >();
	}

	// get(_idx)

	inline void* get(size_t idx)
	{
		switch (idx)
		{
#define _CASE(_x) case json_type:: _x : return &get<json_type:: ##_x>()
			_CASE(array);
			_CASE(object);

			_CASE(num_double);
			_CASE(num_i32);
			_CASE(boolean);
			_CASE(string);

			_CASE(num_float);
			_CASE(num_ui32);
			_CASE(num_i64);
			_CASE(num_ui64);
#undef _CASE
		default:
			break;
		}
		return nullptr;
	}

	template<typename _t>
	bool is()const noexcept { return std::holds_alternative<_t>(_data); }
	template<size_t _idx>
	bool is()const noexcept
	{
		return is<std::variant_alternative_t<_idx, decltype(_data)> >();
	}

	// operator []

	// 如果超出数组范围，会更改大小
	_basic_json& operator[](size_t idx)
	{
		auto& arr = get<array>();
		if (idx >= arr.size())arr.resize(idx + 1);
		return arr[idx];
	}
	const _basic_json& operator[](size_t idx)const { return get<array>()[idx]; }

	_basic_json& operator[](const string_t& key) { return get<object>()[key]; }
	const _basic_json& operator[](const string_t& key)const
	{
		const auto& obj = get<object>();
		const auto& it = obj.find(key);
		return (it == obj.end()) ? _make_tmp<_basic_json>() : it->second;
	}

	// 不使用 string_char_t* 以防 0 被错误识别
	template <typename _t>
	_basic_json& operator[](const _t* const key)
	{
		return this->operator[](std::move(string_t(key)));
	}
	template <typename _t>
	const _basic_json& operator[](const _t* const key)const
	{
		return this->operator[](std::move(string_t(key)));
	}

	// at

	_basic_json& at(size_t idx) { return get<array>().at(idx); }
	const _basic_json& at(size_t idx)const { return get<array>().at(idx); }

	_basic_json& at(const string_t& key) { return get<object>().at(key); }
	const _basic_json& at(const string_t& key)const { return get<object>().at(key); }


private:

	friend class json_pointer;

	template<typename _t>
	static inline const _t& _make_tmp()
	{
		static const _t tmp = _t();
		return tmp;
	}

	template<typename _t>
	bool _ensure_is()
	{
		if (!is<_t>())
		{
			if (!is<nullptr_t>()) {} // TODO : throw err
			_data = _t();
		}
		return true;
	}
	template<typename _t>
	inline bool _ensure_is()const
	{
		if (is<_t>())return true;
		if (!is<nullptr_t>()) {} // TODO : throw err
		return false;
	}

	class _my_initializer_list
	{
	public:

		_my_initializer_list(const _basic_json& x) :_data(x) {}

		/*
		* {a,b,c} : 这种情况会对每一个元素调用该函数来初始化
		*  ^
		*/
		template <typename _t>
		_my_initializer_list(const _t& x) : _data(x) {}
		/*
		* (a,b,c) 或 {}
		* ^^^^^^^
		* 会调用该函数来初始化
		*/
		template <typename... _ts>
		_my_initializer_list(const _ts &...args) : _data(array({ _basic_json(args)... })) {}
		
		_my_initializer_list(std::initializer_list<_my_initializer_list> x)
		{
			/*
			* 形如 {{"xxx",...},{"yyy",...},...} 的数据会被视为 object
			*/
			bool is_obj = true;
			for (auto& it : x)
			{
				const auto& node = it._data;
				if ((!node.is<json_type::array>()) || node.get<_basic_json::array>().size() != 2)
				{
					is_obj = false;
					break;
				}
				if (!node.get<_basic_json::array>()[0].is<json_type::string>())
				{
					is_obj = false;
					break;
				}
			}
			if (is_obj)
			{
				_data = _basic_json::object();
				auto& dest = _data.get<_basic_json::object>();
				for (auto& it : x)
				{
					dest.insert(
						{
							it._data.get<_basic_json::array>()[0].get<std::string>(),
							it._data.get<_basic_json::array>()[1]
						}
					);
				}
				return;
			}
			_data = _basic_json::array();
			auto& dest = _data.get<_basic_json::array>();
			for (auto& it : x)
			{
				dest.push_back(it._data);
			}
		}

		_basic_json& data()& { return _data; }
		const _basic_json& data() const& { return _data; }

		_basic_json&& data()&& { return std::move(_data); }

	private:
		_basic_json _data;
	};

	void _assign(const _my_initializer_list& x) { assign(x.data()); }
};

class json_pointer
{
public:

	explicit json_pointer(const std::string& s = "")
	{
		_split(s, _data);
	}

	inline bool empty()const noexcept { return _data.empty(); }

	// operator

	json_pointer& operator /= (const std::string& key)
	{
		push_back(key);
		return *this;
	}
	json_pointer& operator /= (size_t idx)
	{
		return *this /= std::to_string(idx);
	}
	json_pointer& operator /= (const json_pointer& x)
	{
		_data.insert(_data.end(), x._data.begin(), x._data.end());
		return *this;
	}

	friend json_pointer operator/(json_pointer x, const json_pointer& y)
	{
		return x /= y;
	}
	friend json_pointer operator/(json_pointer x, const std::string& y)
	{
		return x /= y;
	}
	friend json_pointer operator/(json_pointer x, size_t y)
	{
		return x /= y;
	}

	bool operator==(const json_pointer& x)const
	{
		return _data == x._data;
	}
	bool operator!=(const json_pointer& x)const
	{
		return !(*this == x);
	}

	inline operator std::string()const
	{
		return to_string();
	}

	// to_string

	std::string to_string()const
	{
		std::string res;
		for (auto it : _data)
		{
			res += '/';
			res += _escape(it);
		}
		return res;
	}

	// push/pop/back

	void pop_back()
	{
		if (empty())return;
		_data.pop_back();
	}
	void push_back(const std::string& s, bool need_unescape = true)
	{
		_data.push_back(need_unescape ? _unescape(s) : s);
	}

	const std::string& back() const
	{
		if (empty())
		{
			static const std::string tmp;
			return tmp;
		}
		return _data.back();
	}

	// parent

	json_pointer parent()const
	{
		if (empty())return *this;
		json_pointer res = *this;
		res.pop_back();
		return res;
	}

//private:

	using _data_t = std::vector<std::string>;
	_data_t _data;

	static void _replace_substr(std::string& s, const std::string& from, const std::string& to)
	{
		for (auto pos = s.find(from); pos != std::string::npos;
			s.replace(pos, from.size(), to), pos = s.find(from, pos + to.size())
			) {
		}
	}

	static void _escape_on(std::string& s)
	{
		_replace_substr(s, "~", "~0");
		_replace_substr(s, "/", "~1");
	}
	static std::string _escape(std::string s)
	{
		_escape_on(s);
		return s;
	}
	static void _unescape_on(std::string& s)
	{
		_replace_substr(s, "~0", "~");
		_replace_substr(s, "~1", "/");
	}
	static std::string _unescape(std::string s)
	{
		_unescape_on(s);
		return s;
	}

	static void _split(const std::string& s, _data_t& out)
	{
		if (s.empty())return;
		if (s[0] != '/')return;
		if (s.size() == 1) // "/" 
		{
			out.push_back("");
			return;
		}
		std::istringstream iss(s);
		iss.get(); // 丢掉第一个 '/'
		while (1)
		{
			out.push_back("");
			auto& tok = out[out.size() - 1];
			if (!std::getline(iss, tok, '/'))
			{
				out.pop_back();
				break;
			}
			_unescape_on(tok);
		}
	}

	static size_t _get_idx_from(const std::string& s)
	{
		unsigned long long res = 0;
		try
		{
			res = std::stoull(s);
		}
		catch (std::out_of_range&)
		{
			return -1;
		}
		if (res >= static_cast<unsigned long long>(std::numeric_limits<size_t>::max()))
			return -1;
		return static_cast<size_t>(res);
	}

	template<typename _json_t>
	_json_t& _get_from(_json_t& x, bool use_at = true)const
	{
		_json_t* now = &x;
		for (auto& tok : _data)
		{
			if (now->is_val())
			{
				const bool is_num = std::all_of(tok.begin(), tok.end(),
					[](const unsigned char x)
					{
						return std::isdigit(x);
					}
				);
				if (is_num || tok == "-")now->assign(_json_t::array());
				else now->assign(_json_t::object());
			}

			if (now->is_obj())
			{
				now = use_at
					? &now->at(tok)
					: &now->operator[](tok);
			}
			else if (now->is_arr())
			{
				size_t idx = (tok == "-")
					? now->get<json_type::array>().size()
					: _get_idx_from(tok);
				now = use_at
					? &now->at(idx)
					: &now->operator[](idx);
			}
		}
		return *now;
	}

};

using json = _basic_json<>;

};

inline hpjson::json_pointer operator "" _json_pointer(const char* s, size_t len)
{
	return hpjson::json_pointer(std::string(s, len));
}