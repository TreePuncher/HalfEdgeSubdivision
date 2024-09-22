#include <Components.hpp>
#include <Type.hpp>
#include <Handle.hpp>
#include <tuple>

namespace FlexKit
{
	template<typename ... TY_Fields>
	struct MultiField
	{
		using FieldContainer		= std::tuple<TY_Fields*...>;
		using ConstFieldContainer	= std::tuple<const TY_Fields*...>;

		MultiField() = default;

		MultiField(iAllocator& IN_allocator) : 
			allocator{ IN_allocator } {}

		MultiField(const MultiField& rhs) : 
			allocator	{ rhs.allocator }
		{
			reserve(rhs.size());

			for (auto&& values : rhs)
				push_back(values);
		}

		MultiField(MultiField&& rhs) noexcept :
			allocator	{ rhs.allocator },
			fields		{ rhs.fields	},
			max			{ rhs.max		},
			used		{ rhs.used		}
		{
			rhs.fields		= FieldContainer{};
			rhs.max			= 0;
			rhs.used		= 0;
			rhs.allocator	= nullptr;
		}

		MultiField& operator = (const MultiField& rhs)
		{
			FK_ASSERT(this != &rhs);

			if (!allocator)
				allocator = rhs.allocator;

			reserve(rhs.size());

			for (auto&& values : rhs)
				push_back(values);

			return *this;
		}

		MultiField& operator = (MultiField&& rhs)
		{
			allocator	= rhs.allocator;
			fields		= rhs.fields;
			max			= rhs.max;
			used		= rhs.used;

			rhs.fields		= FieldContainer{};
			rhs.max			= 0;
			rhs.used		= 0;
			rhs.allocator	= nullptr;
			return *this;
		}

		~MultiField()
		{
			Release();
			allocator = nullptr;
		}


		void Release()
		{
			resize(0);

			_Apply(
				[&]<size_t fieldID, typename Field_TY>(auto * field)
				{
					allocator->free(field);
					std::get<fieldID>(fields) = nullptr;
				});
		}


		void _Apply(auto&& FN)
		{
			auto helper = [&]<size_t ... indexes>(std::index_sequence<indexes...> sequence)
			{
				auto action = [&]<size_t fieldID>()
				{
					auto* field = std::get<fieldID>(fields);

					FN.operator()<
						fieldID, 
						std::remove_pointer_t<decltype(field)>
					>(field);
				};

				(action.operator()<indexes>(), ...);
			};

			constexpr size_t end = sizeof ... (TY_Fields);
			return helper(std::make_index_sequence<end>());
		}


		auto _Apply_ret(auto&& FN) const
		{
			auto helper = [&]<size_t ... indexes>(std::index_sequence<indexes...> sequence) -> auto
			{
				auto action = [&]<size_t fieldID>() -> auto
				{
					auto* field = std::get<fieldID>(fields);

					return 
						FN.operator()<
							fieldID, 
							std::remove_pointer_t<decltype(field)>
						>(field);
				};

				auto ret = std::make_tuple(action.operator()<indexes>()...);
				return ret;
			};

			constexpr size_t end = sizeof ... (TY_Fields);
			return helper(std::make_index_sequence<end>());
		}


		auto operator [] (size_t idx) noexcept
		{
			FK_ASSERT(idx < used);

			return _Apply_ret(
				[&]<size_t fieldIdx, typename TY_field>(auto* field) -> TY_field&
				{
					return field[idx];
				});
		}
	

		uint64_t push_back(TY_Fields&& ... args)
		{
			FK_ASSERT(allocator != nullptr);

			if (used + 1 > max)
				Expand();

			auto argsTuple = std::forward_as_tuple(args...);

			size_t idx = used++;
			_Apply(
				[&]<size_t fieldID, typename Field_TY>(auto field)
				{
					std::construct_at(field + idx, std::get<fieldID>(argsTuple));
				});

			return idx;
		}


		uint64_t push_back(const TY_Fields& ... args)
		{
			FK_ASSERT(allocator != nullptr);

			if (used + 1 > max)
				Expand();

			auto argsTuple = std::forward_as_tuple(args...);

			size_t idx = used++;
			_Apply(
				[&]<size_t fieldID, typename Field_TY>(auto field)
				{
					std::construct_at(field + idx, std::get<fieldID>(argsTuple));
				});

			return idx;
		}


		template<typename ... TY_args>
		uint64_t push_back(const std::tuple<TY_args...>& argsTuple)
		{
			FK_ASSERT(allocator != nullptr);

			if (used + 1 > max)
				Expand();

			size_t idx = used++;
			_Apply(
				[&]<size_t fieldID, typename Field_TY>(auto field)
				{
					std::construct_at(field + idx, std::get<fieldID>(argsTuple));
				});

			return idx;
		}


		void pop_back()
		{
			FK_ASSERT(used	> 1);
			FK_ASSERT(allocator != nullptr);

			--used;

			_Apply(
				[&]<size_t fieldID, typename Field_TY>(auto field)
				{
					std::destroy_at(field + used);
				});
		}

		auto front()
		{
			FK_ASSERT(used >= 1);
			return (*this)[0];
		}

		auto back()
		{
			FK_ASSERT(used >= 1);
			return (*this)[used - 1];
		}


		void Expand()
		{
			const size_t newMax = max > 0 ? 2 * max : 8;
			reserve(newMax);
		}


		void reserve(const size_t newSize)
		{
			FK_ASSERT(allocator != nullptr);

			if (newSize < max)
				return;

			const size_t newMax = newSize;

			auto newFields = _Apply_ret(
				[&]<size_t fieldID, typename Field_TY>(auto _) -> Field_TY*
				{
					auto temp = (Field_TY*)allocator->_aligned_malloc(newMax * sizeof(Field_TY));

					if (!temp)
						throw std::runtime_error("Failed to allocate");
				
					return temp;
				});

			_Apply(
				[&]<size_t fieldID, typename Field_TY>(auto field)
				{
					auto temp = std::get<fieldID>(newFields);

					if (field)
					{
						std::move(field, field + used, temp);
						allocator->free(field);
					}

					std::get<fieldID>(fields) = temp;
				});

			max = newMax;
		}


		void resize(const size_t newSize)
		{
			while (newSize > used)
				pop_back();
		}


		template<typename TY>
		struct Iterator_t
		{
			static void _Apply(auto&& FN, auto& _ptrs)
			{
				auto helper = [&]<size_t ... indexes>(std::index_sequence<indexes...> sequence)
				{
					auto action = [&]<size_t fieldID>()
					{
						auto* field = std::get<fieldID>(_ptrs);

						FN.operator()<
							fieldID, 
							std::remove_pointer_t<decltype(field)>
						>(field);
					};

					(action.operator()<indexes>(), ...);
				};

				constexpr size_t end = std::tuple_size_v<TY>;
				return helper(std::make_index_sequence<end>());
			}


			static auto _Apply_ret(auto&& FN, auto& _ptrs)
			{
				auto helper = [&]<size_t ... indexes>(std::index_sequence<indexes...> sequence) -> auto
				{
					auto action = [&]<size_t fieldID>() -> auto
					{
						auto* field = std::get<fieldID>(_ptrs);

						return 
							FN.operator()<
								fieldID, 
								std::remove_pointer_t<decltype(field)>
							>(field);
					};

					auto ret = std::make_tuple(action.operator()<indexes>()...);
					return ret;
				};

				constexpr size_t end = std::tuple_size_v<TY>;
				return helper(std::make_index_sequence<end>());
			}


			static auto _Apply_ret_ref(auto&& FN, TY& _ptrs)
			{
				auto helper = [&]<size_t ... indexes>(std::index_sequence<indexes...> sequence)
				{
					auto action = [&]<size_t fieldID>() -> decltype(auto)
					{
						auto field = std::get<fieldID>(_ptrs);

						return
							FN.operator()<
								fieldID,
								std::remove_pointer_t<decltype(field)>
							>(field);
					};

					return std::tie(action.operator()<indexes>()...);
				};

				constexpr size_t end = std::tuple_size_v<TY>;
				return helper(std::make_index_sequence<end>());
			}

			Iterator_t& operator = (const Iterator_t& rhs) = default;

			Iterator_t& operator ++ ()
			{
				_Apply(
					[&]<size_t fieldID, typename Field_TY>(auto _ptr)
					{
						std::get<fieldID>(_ptrs) = ++_ptr;
					}, _ptrs);

				return *this;
			}

			Iterator_t& operator += (size_t offset)
			{
				_Apply(
					[&]<size_t fieldID, typename Field_TY>(auto _ptr)
					{
						std::get<fieldID>(_ptrs) = _ptr + offset;
					}, _ptrs);

				return *this;
			}

			Iterator_t& operator ++ (int)
			{
				Iterator_t next{ _ptrs };

				_Apply(
					[&]<size_t fieldID, typename Field_TY>(auto _ptr)
					{
						std::get<fieldID>(next._ptrs) = ++_ptr;
					}, _ptrs);

				return next;
			}

			Iterator_t operator + (size_t offset)
			{
				Iterator_t next{ _ptrs };

				_Apply(
					[&]<size_t fieldID, typename Field_TY>(auto _ptr)
					{
						std::get<fieldID>(next._ptrs) = _ptr + offset;
					}, _ptrs);

				return next;
			}

			Iterator_t& operator -- ()
			{
				_Apply(
					[&]<size_t fieldID, typename Field_TY>(auto _ptr)
					{
						std::get<fieldID>(_ptrs) = --_ptr;
					}, _ptrs);

				return *this;
			}

			Iterator_t& operator -- (int)
			{
				Iterator_t next{ _ptrs };

				_Apply(
					[&]<size_t fieldID, typename Field_TY>(auto _ptr)
					{
						std::get<fieldID>(next._ptrs) = --_ptr;
					}, * this);

				return next;
			}


			bool operator < (const Iterator_t& rhs) const
			{
				return std::get<0>(_ptrs) < std::get<0>(rhs._ptrs);
			}

			bool operator > (const Iterator_t& rhs) const
			{
				return std::get<0>(_ptrs) > std::get<0>(rhs._ptrs);
			}

			bool operator == (const Iterator_t& rhs) const
			{
				return std::get<0>(_ptrs) == std::get<0>(rhs._ptrs);
			}

			bool operator != (const Iterator_t& rhs) const
			{
				return std::get<0>(_ptrs) != std::get<0>(rhs._ptrs);
			}

			auto operator * ()
			{
				auto temp = 
					_Apply_ret_ref(
					[&]<size_t fieldID, typename Field_TY>(auto _ptr) -> decltype(auto)
					{
						return *_ptr;
					}, _ptrs);

				return temp;
			}

			TY _ptrs;
		};

		using Iterator		= Iterator_t<FieldContainer>;
		using ConstIterator = Iterator_t<ConstFieldContainer>;

		auto begin()
		{
			return 
				Iterator{ 
					_Apply_ret(
					[]<size_t fieldID>(auto* field)
					{
						return field;
					}) };
		}


		auto end()
		{
			return
				Iterator{ 
					_Apply_ret(
					[&]<size_t fieldID>(auto* field)
					{
						return field + used;
					}) };
		}

		auto begin() const
		{
			return 
				ConstIterator{
					_Apply_ret(
					[]<size_t fieldID>(const auto* field)
					{
						return field;
					}) };
		}


		auto end() const
		{
			return
				ConstIterator{
					_Apply_ret(
					[&]<size_t fieldID>(auto* field)
					{
						return field + used;
					}) };
		}

		template<typename TY_iterator>
		struct View
		{
			TY_iterator _begin;
			TY_iterator _end;

			auto operator [](size_t idx) noexcept -> decltype(auto)
			{
				return *(_begin + idx);
			}

			auto operator [](size_t idx) const noexcept -> decltype(auto)
			{
				return *(_begin + idx);
			}

			auto begin()	{ return _begin; }
			auto end()		{ return _end; }
		};

		template<size_t ... fieldIDs>
		auto Slice(size_t begin, size_t end) noexcept
		{
			using TY_fields = decltype(std::make_tuple(std::get<fieldIDs>(fields)...));
			TY_fields slice = std::make_tuple(std::get<fieldIDs>(fields)...);
			Iterator_t<TY_fields> itr{ slice };

			return 
				View<Iterator_t<TY_fields>>{
					._begin	= itr + begin,
					._end	= itr + end
				};
		}

		template<size_t ... fieldIDs>
		auto Slice() noexcept
		{
			return Slice<fieldIDs...>(0, used);
		}

		template<size_t ... fieldIDs>
		auto Slice(size_t begin, size_t end) const noexcept
		{
			auto helper = [&]<typename ... TY_args>(std::tuple<TY_args*...>)
			{
				return std::tuple<const TY_args*...>(std::get<fieldIDs>(fields)...);
			};

			std::tuple non_const_tuple	= std::tuple{ std::get<fieldIDs>(fields)... };
			auto slice					= helper(non_const_tuple);

			Iterator_t<decltype(slice)> itr{ slice };

			return 
				View<Iterator_t<decltype(slice)>>{
					._begin	= itr + begin,
					._end	= itr + end
				};
		}

		template<size_t ... fieldIDs>
		auto Slice() const noexcept
		{
			return SliceFields<fieldIDs...>(0, used);
		}

		size_t size() const noexcept { return used; }

		size_t			max			= 0;
		size_t			used		= 0;
		iAllocator*		allocator	= nullptr;

		FieldContainer			fields;
	};

	template<typename TY_Handle, ComponentID ID, typename TY_EventHandler, typename ... TY_Fields>
	class MultiFieldComponent_t : public Component<MultiFieldComponent_t<TY_Handle, ID, TY_EventHandler, TY_Fields...>, ID>
	{
	public:
		using ThisType		= MultiFieldComponent_t<TY_Handle, ID, TY_EventHandler, TY_Fields...>;
		using EventHandler	= TY_EventHandler;

		struct MultiFieldComponentView : public ComponentView_t<MultiFieldComponent_t>
		{
			MultiFieldComponentView(GameObject& gameObject, TY_Handle IN_handle) : handle{ IN_handle } {}
			MultiFieldComponentView(GameObject& gameObject, TY_Fields&& ... args) : handle{ MultiFieldComponentView::GetComponent().Create(gameObject, std::forward<TY_Fields>(args)...) } {}


			MultiFieldComponentView(const MultiFieldComponentView&)					= delete;
			MultiFieldComponentView& operator = (const MultiFieldComponentView&)	= delete;

			MultiFieldComponentView(MultiFieldComponentView&&)						= delete;
			MultiFieldComponentView& operator = (MultiFieldComponentView&&)			= delete;

			void Release()
			{
				MultiFieldComponent_t::GetComponent().Remove(handle);
				handle = InvalidHandle;
			}

			decltype(auto) operator -> ()
			{
				return &GetData();
			}

			decltype(auto) GetData()
			{
				return MultiFieldComponentView::GetComponent()[handle];
			}

			template<size_t ... FieldIDs>
			decltype(auto) Slice()
			{
				return MultiFieldComponentView::GetComponent()[handle];
			}

			TY_Handle handle = InvalidHandle;
		};

		template<typename ... TY_args>
		MultiFieldComponent_t(iAllocator* allocator, TY_args&&... args) :
			eventHandler	{ std::forward<TY_args>(args)... },
			fields			{ allocator },
			handles			{ allocator } 
		{
			fields.push_back(1);
		}

		MultiFieldComponent_t(iAllocator* allocator) :
			fields	{ *allocator },
			handles	{  allocator } 
		{
		}

		~MultiFieldComponent_t()
		{
		}

		struct elementData
		{
			TY_Handle		handle;
		};

		using View = MultiFieldComponentView;


		TY_Handle Create(GameObject& gameObject, const TY_Fields& ... initial)
		{
			auto handle = handles.GetNewHandle();
			handles[handle] = fields.push_back(handle, initial...);

			return handle;
		}


		TY_Handle Create(GameObject& gameObject, TY_Fields&& ... initial)
		{
			auto handle = handles.GetNewHandle();
			handles[handle] = fields.push_back(handle, initial...);

			return handle;
		}


		TY_Handle Create(GameObject& gameObject) requires ComponentVoidCreator<TY_EventHandler>
		{
			auto handle = handles.GetNewHandle();
			std::apply(
				[&](auto ...args) 
				{
					handles[handle] = fields.push_back(handle, args...);
				}, eventHandler(gameObject));

			return handle;
		}

		auto operator [](TY_Handle handle) -> decltype(auto)
		{
			return fields.Slice<1, sizeof ... (TY_Fields)>()[handles[handle]];
		}


		auto operator [](TY_Handle handle) const -> decltype(auto)
		{
			return fields.Slice<1, sizeof ... (TY_Fields)>()[handles[handle]];
		}

		void AddComponentView(GameObject& gameObject, ValueMap values, const std::byte* buffer, const size_t bufferSize, iAllocator* allocator) override
		{
			eventHandler.OnCreateView(gameObject, values, buffer, bufferSize, allocator);
		}


		void FreeComponentView(void* _ptr) override
		{
			reinterpret_cast<View*>(_ptr)->Release();
		}

		void Remove(TY_Handle handle)
		{
			auto lastElement = std::move(fields.back());
			fields[handles[handle]] = std::move(lastElement);
			fields.pop_back();

			handles[std::get<0>(lastElement)] = handles[handle];
			handles.RemoveHandle(handle);
		}

		auto& GetFactory() noexcept { return eventHandler; }

		auto begin()	{ return fields.begin(); }
		auto end()		{ return fields.end(); }


		template<size_t ... FieldIDs> decltype(auto) Slice() noexcept { return fields.Slice<(FieldIDs + 1)...>(); }
		template<size_t ... FieldIDs> decltype(auto) Slice(size_t begin, size_t end) noexcept { return fields.Slice<(FieldIDs + 1)...>(begin, end); }


		size_t size() const noexcept { return fields.size(); }

		HandleUtilities::HandleTable<TY_Handle>	handles;
		MultiField<TY_Handle, TY_Fields...>		fields;
		NO_UNIQUE_ADDRESS TY_EventHandler		eventHandler;
	};

	struct MultiFieldComponentEventHandler
	{
		static decltype(auto) OnCreate(GameObject&, auto&& args)
		{
			return args;
		}

		static void OnCreateView(GameObject& gameObject, ValueMap user_ptr, const std::byte* buffer, const size_t bufferSize, iAllocator* allocator)
		{
		}
	};
}

struct TestData
{
	int hi;
};

constexpr FlexKit::ComponentID TestComponentID = GetTypeGUID(TestData);

using TestComponentHandle	= FlexKit::Handle_t<32, TestComponentID>;
using TestComponent			= FlexKit::BasicComponent_t<TestData, TestComponentHandle, TestComponentID>;


using TestMultiFieldComponent = FlexKit::MultiFieldComponent_t<TestComponentHandle, TestComponentID, FlexKit::MultiFieldComponentEventHandler, TestData, TestData>;
