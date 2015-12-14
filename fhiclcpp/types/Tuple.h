#ifndef fhiclcpp_types_Tuple_h
#define fhiclcpp_types_Tuple_h

#include "fhiclcpp/detail/printing_helpers.h"
#include "fhiclcpp/type_traits.h"
#include "fhiclcpp/types/Atom.h"
#include "fhiclcpp/types/ConfigPredicate.h"
#include "fhiclcpp/types/detail/NameStackRegistry.h"
#include "fhiclcpp/types/detail/SequenceBase.h"
#include "fhiclcpp/types/detail/type_traits_error_msgs.h"

#include <memory>
#include <string>
#include <utility>

namespace fhicl {

  namespace tuple_detail {

    // Auxiliary struct for accepting either
    //
    //  (1) {1, false, "Henry"}
    //  (2) std::tuple<int,bool,string>{2, true, "Hannah"}
    //
    // default values for Sequence<T,N>

    template <typename ... DEFAULTS>
    class ValueHolder {
    public:
      ValueHolder(DEFAULTS ... defaults)
        : holder_{ std::forward_as_tuple(defaults...) }
      {}

      ValueHolder(std::tuple<DEFAULTS...> const& tup)
        : holder_{tup}
      {}

      template<std::size_t I>
      auto const& get() const { return std::get<I>(holder_); }

    private:
      std::tuple<DEFAULTS...> holder_;
    };

  }

  class ParameterSet;

  //==================================================================
  // e.g. Tuple<int,double,bool> ====> std::tuple<int,double,bool>
  //

  template<typename ... TYPES>
  class Tuple final :
    public  detail::SequenceBase,
    private detail::RegisterIfTableMember {
  public:

    using dtype = tuple_detail::ValueHolder< typename tt::fhicl_type<TYPES>::dtype ... >;
    using rtype = std::tuple< tt::return_type<TYPES>... >;
    using ftype = std::tuple< std::shared_ptr<tt::fhicl_type<TYPES>>...  >;

    explicit Tuple(Name&& name);
    explicit Tuple(Name&& name, Comment&& comment);
    explicit Tuple(Name&& name, Comment&& comment, std::function<bool()> maybeUse);

    // c'tors supporting defaults;
    explicit Tuple(Name&& name, dtype const& defaults);
    explicit Tuple(Name&& name, Comment&& comment, dtype const& defaults);
    explicit Tuple(Name&& name, Comment&& comment, std::function<bool()> maybeUse, dtype const& defaults);

    auto operator()() const;

    template<std::size_t I>
    auto get() const { return (*std::get<I>(value_))(); }

  private:

    //=================================================================
    // aliases
    using TUPLE  = std::tuple<tt::fhicl_type<TYPES>...>;
    using UTUPLE = std::tuple<TYPES...>;

    ftype value_;

    std::size_t get_size() const override { return std::tuple_size<ftype>(); }

    void do_set_value(fhicl::ParameterSet const&, bool /*trimParents*/) override {}

    //===================================================================
    // iterate over tuple elements
    using PW_non_const = detail::ParameterWalker<tt::const_flavor::require_non_const>;
    using PW_const = detail::ParameterWalker<tt::const_flavor::require_const>;

    void visit_element(PW_non_const&){}

    template <typename E, typename ... T>
    void visit_element(PW_non_const& pw, E& elem, T& ... others)
    {
      static_assert(!tt::is_table_fragment<E>::value, NO_NESTED_TABLE_FRAGMENTS);
      static_assert(!tt::is_optional_parameter<E>::value, NO_OPTIONAL_TYPES );
      pw(*elem);
      visit_element(pw, others...);
    }

    template <std::size_t ... I>
    void iterate_over_tuple(PW_non_const& pw, std::index_sequence<I...>)
    {
      visit_element(pw, std::get<I>(value_)...);
    }

    void do_walk_elements(PW_non_const& pw)
    {
      iterate_over_tuple(pw, std::index_sequence_for<TYPES...>{});
    }

    void visit_element(PW_const&) const {}

    template <typename E, typename ... T>
    void visit_element(PW_const& pw, E const& elem, T const& ... others) const
    {
      static_assert(!tt::is_table_fragment<E>::value, NO_NESTED_TABLE_FRAGMENTS);
      static_assert(!tt::is_optional_parameter<E>::value, NO_OPTIONAL_TYPES );
      pw(*elem);
      visit_element(pw, others...);
    }

    template <std::size_t ... I>
    void iterate_over_tuple(PW_const& pw, std::index_sequence<I...>) const
    {
      visit_element(pw, std::get<I>(value_)...);
    }

    void do_walk_elements(PW_const& pw) const
    {
      iterate_over_tuple(pw, std::index_sequence_for<TYPES...>{});
    }

    //===============================================================
    // finalizing tuple elements
    void finalize_tuple_elements(std::size_t){}

    template <typename E, typename ... T>
    void finalize_tuple_elements(std::size_t i, E& elem, T& ... others)
    {
      using elem_ftype = typename E::element_type;
      static_assert(!tt::is_table_fragment<elem_ftype>::value, NO_NESTED_TABLE_FRAGMENTS);
      static_assert(!tt::is_optional_parameter<elem_ftype>::value, NO_OPTIONAL_TYPES );

      elem = std::make_shared<elem_ftype>( Name::sequence_element(i) );
      finalize_tuple_elements(++i, others...);
    }

    template <std::size_t ... I>
    void finalize_elements(std::index_sequence<I...>)
    {
      finalize_tuple_elements(0, std::get<I>(value_)...);
    }


    //===================================================================
    // filling tuple elements from default
    template <size_t I>
    std::enable_if_t<(I >= std::tuple_size<TUPLE>::value)>
    fill_tuple_element(dtype const&)
    {}

    template <size_t I>
    std::enable_if_t<(I < std::tuple_size<TUPLE>::value)>
    fill_tuple_element(dtype const& defaults)
    {
      using elem_utype = std::tuple_element_t<I,UTUPLE>;
      static_assert(!tt::is_table<elem_utype>::value, NO_DEFAULTS_FOR_TABLE);
      static_assert(!tt::is_sequence_type<elem_utype>::value, NO_STD_CONTAINERS);

      auto & elem = std::get<I>(value_);
      elem = std::make_shared<tt::fhicl_type<elem_utype>>( Name::sequence_element(I), defaults.get<I>() );
      fill_tuple_element<I+1>(defaults);
    }

    void fill_tuple_elements(dtype const& default_values)
    {
      fill_tuple_element<0>(default_values);
    }

    //===================================================================
    // filling return type
    template <size_t I, typename rtype>
    std::enable_if_t<(I >= std::tuple_size<TUPLE>::value)>
    fill_return_element(rtype &) const
    {}

    template <size_t I, typename rtype>
    std::enable_if_t<(I < std::tuple_size<TUPLE>::value)>
    fill_return_element(rtype & result) const
    {
      std::get<I>(result) = (*std::get<I>(value_))();
      fill_return_element<I+1>(result);
    }

    void assemble_rtype(rtype & result) const
    {
      fill_return_element<0>( result );
    }

  }; // class Tuple

  //================= IMPLEMENTATION =========================
  //
  template<typename...TYPES>
  Tuple<TYPES...>::Tuple(Name&& name)
    : Tuple{std::move(name), Comment("")}
  {}

  template<typename ... TYPES>
  Tuple<TYPES...>::Tuple(Name&& name, Comment&& comment)
    : SequenceBase{std::move(name), std::move(comment), value_type::REQUIRED, par_type::TUPLE, detail::AlwaysUse()}
    , RegisterIfTableMember{this}
  {
    finalize_elements(std::index_sequence_for<TYPES...>{});
    NameStackRegistry::end_of_ctor();
  }

  template<typename ... TYPES>
  Tuple<TYPES...>::Tuple(Name&& name, Comment&& comment, std::function<bool()> maybeUse)
    : SequenceBase{std::move(name), std::move(comment), value_type::REQUIRED_CONDITIONAL, par_type::TUPLE, maybeUse}
    , RegisterIfTableMember{this}
  {
    finalize_elements(std::index_sequence_for<TYPES...>{});
    NameStackRegistry::end_of_ctor();
  }

  // c'tors supporting defaults

  template<typename ... TYPES>
  Tuple<TYPES...>::Tuple(Name&& name, dtype const& defaults)
    : Tuple{std::move(name), Comment(""), defaults}
  {}

  template<typename ... TYPES>
  Tuple<TYPES...>::Tuple(Name&& name, Comment&& comment, dtype const& defaults)
    : SequenceBase{std::move(name), std::move(comment), value_type::REQUIRED, par_type::TUPLE, detail::AlwaysUse()}
    , RegisterIfTableMember{this}
  {
    fill_tuple_elements(defaults);
    NameStackRegistry::end_of_ctor();
  }


  template<typename ... TYPES>
  Tuple<TYPES...>::Tuple(Name&& name, Comment&& comment, std::function<bool()> maybeUse, dtype const& defaults)
    : SequenceBase{std::move(name), std::move(comment), value_type::REQUIRED_CONDITIONAL, par_type::TUPLE, maybeUse}
    , RegisterIfTableMember{this}
  {
    fill_tuple_elements(defaults);
    NameStackRegistry::end_of_ctor();
  }

 template<typename ... TYPES>
  auto
  Tuple<TYPES...>::operator()() const
  {
    rtype result;
    assemble_rtype(result);
    return result;
  }

}

#endif

// Local variables:
// mode: c++
// End:
