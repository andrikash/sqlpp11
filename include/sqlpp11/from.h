/*
 * Copyright (c) 2013-2015, Roland Bock
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *   Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SQLPP_FROM_H
#define SQLPP_FROM_H

#include <sqlpp11/table_ref.h>
#include <sqlpp11/type_traits.h>
#include <sqlpp11/no_data.h>
#include <sqlpp11/interpretable_list.h>
#include <sqlpp11/interpret_tuple.h>
#include <sqlpp11/logic.h>
#include <sqlpp11/detail/sum.h>
#include <sqlpp11/policy_update.h>
#include <sqlpp11/dynamic_join.h>

namespace sqlpp
{
  // FROM DATA
  template <typename Database, typename Table>
  struct from_data_t
  {
    from_data_t(Table table) : _table(table)
    {
    }

    from_data_t(const from_data_t&) = default;
    from_data_t(from_data_t&&) = default;
    from_data_t& operator=(const from_data_t&) = default;
    from_data_t& operator=(from_data_t&&) = default;
    ~from_data_t() = default;

    Table _table;
    interpretable_list_t<Database> _dynamic_tables;
  };

  // FROM
  template <typename Database, typename Table>
  struct from_t
  {
    using _traits = make_traits<no_value_t, tag::is_from>;
    using _nodes = detail::type_vector<Table>;
    using _is_dynamic = is_database<Database>;

    // Data
    using _data_t = from_data_t<Database, Table>;

    // Member implementation with data and methods
    template <typename Policies>
    struct _impl_t
    {
      // workaround for msvc bug https://connect.microsoft.com/VisualStudio/Feedback/Details/2091069
      _impl_t() = default;
      _impl_t(const _data_t& data) : _data(data)
      {
      }

      template <typename DynamicJoin>
      void add(DynamicJoin dynamicJoin)
      {
        static_assert(_is_dynamic::value, "from::add() must not be called for static from()");
        static_assert(is_dynamic_join_t<DynamicJoin>::value, "invalid argument in from::add(), expected dynamic_join");
        using _known_tables = provided_tables_of<Table>;  // Hint: Joins contain more than one table
        // workaround for msvc bug https://connect.microsoft.com/VisualStudio/feedback/details/2173198
        //		using _known_table_names = detail::transform_set_t<name_of, _known_tables>;
        using _known_table_names = detail::make_name_of_set_t<_known_tables>;
        using _joined_tables = provided_tables_of<DynamicJoin>;
        using _joined_table_names = detail::make_name_of_set_t<_joined_tables>;
        static_assert(detail::is_disjunct_from<_joined_table_names, _known_table_names>::value,
                      "Must not use the same table name twice in from()");
        using _serialize_check = sqlpp::serialize_check_t<typename Database::_serializer_context_t, DynamicJoin>;
        _serialize_check::_();

        using ok = logic::all_t<_is_dynamic::value, is_table_t<DynamicJoin>::value, _serialize_check::type::value>;

        _add_impl(dynamicJoin, ok());  // dispatch to prevent compile messages after the static_assert
      }

    private:
      template <typename DynamicJoin>
      auto _add_impl(DynamicJoin dynamicJoin, const std::true_type&) -> void
      {
        _data._dynamic_tables.emplace_back(from_table(dynamicJoin));
      }

      template <typename DynamicJoin>
      auto _add_impl(DynamicJoin dynamicJoin, const std::false_type&) -> void;

    public:
      _data_t _data;
    };

    // Base template to be inherited by the statement
    template <typename Policies>
    struct _base_t
    {
      using _data_t = from_data_t<Database, Table>;

      // workaround for msvc bug https://connect.microsoft.com/VisualStudio/Feedback/Details/2091069
      template <typename... Args>
      _base_t(Args&&... args)
          : from{std::forward<Args>(args)...}
      {
      }

      _impl_t<Policies> from;
      _impl_t<Policies>& operator()()
      {
        return from;
      }
      const _impl_t<Policies>& operator()() const
      {
        return from;
      }

      template <typename T>
      static auto _get_member(T t) -> decltype(t.from)
      {
        return t.from;
      }

      // FIXME: We might want to check if we have too many tables define in the FROM
      using _consistency_check = consistent_t;
    };
  };

  SQLPP_PORTABLE_STATIC_ASSERT(
      assert_from_not_pre_join_t,
      "from() argument is a pre join, please use an explicit on() condition or unconditionally()");
  SQLPP_PORTABLE_STATIC_ASSERT(assert_from_table_t, "from() argument has to be a table or join expression");
  SQLPP_PORTABLE_STATIC_ASSERT(assert_from_dependency_free_t, "at least one table depends on another table in from()");
  SQLPP_PORTABLE_STATIC_ASSERT(assert_from_no_duplicates_t, "at least one duplicate table name detected in from()");

  SQLPP_PORTABLE_STATIC_ASSERT(assert_from_dynamic_statement_dynamic_t,
                               "dynamic_from must not be called in a static statement");

  template <typename Table>
  struct check_from
  {
    using type = static_combined_check_t<
        static_check_t<not is_pre_join_t<Table>::value, assert_from_not_pre_join_t>,
        static_check_t<is_table_t<Table>::value, assert_from_table_t>,
        static_check_t<required_tables_of<Table>::size::value == 0, assert_from_dependency_free_t>,
        static_check_t<provided_tables_of<Table>::size::value ==
                           detail::make_name_of_set_t<provided_tables_of<Table>>::size::value,
                       assert_from_no_duplicates_t>>;
  };

  template <typename Table>
  using check_from_t = typename check_from<Table>::type;

  template <typename Table>
  using check_from_static_t = check_from_t<Table>;

  template <typename Database, typename Table>
  using check_from_dynamic_t = static_combined_check_t<
      static_check_t<not std::is_same<Database, void>::value, assert_from_dynamic_statement_dynamic_t>,
      check_from_t<Table>>;

  struct no_from_t
  {
    using _traits = make_traits<no_value_t, tag::is_noop>;
    using _nodes = detail::type_vector<>;

    // Data
    using _data_t = no_data_t;

    // Member implementation with data and methods
    template <typename Policies>
    struct _impl_t
    {
      // workaround for msvc bug https://connect.microsoft.com/VisualStudio/Feedback/Details/2091069
      _impl_t() = default;
      _impl_t(const _data_t& data) : _data(data)
      {
      }

      _data_t _data;
    };

    // Base template to be inherited by the statement
    template <typename Policies>
    struct _base_t
    {
      using _data_t = no_data_t;

      // workaround for msvc bug https://connect.microsoft.com/VisualStudio/Feedback/Details/2091069
      template <typename... Args>
      _base_t(Args&&... args)
          : no_from{std::forward<Args>(args)...}
      {
      }

      _impl_t<Policies> no_from;
      _impl_t<Policies>& operator()()
      {
        return no_from;
      }
      const _impl_t<Policies>& operator()() const
      {
        return no_from;
      }

      template <typename T>
      static auto _get_member(T t) -> decltype(t.no_from)
      {
        return t.no_from;
      }

      using _database_t = typename Policies::_database_t;

      template <typename Check, typename T>
      using _new_statement_t = new_statement_t<Check::value, Policies, no_from_t, T>;

      using _consistency_check = consistent_t;

      template <typename Table>
      auto from(Table table) const -> _new_statement_t<check_from_static_t<Table>, from_t<void, from_table_t<Table>>>
      {
        using Check = check_from_static_t<Table>;
        Check{}._();
        return _from_impl<void>(Check{}, table);
      }

      template <typename Table>
      auto dynamic_from(Table table) const
          -> _new_statement_t<check_from_dynamic_t<_database_t, Table>, from_t<_database_t, from_table_t<Table>>>
      {
        using Check = check_from_dynamic_t<_database_t, Table>;
        Check{}._();
        return _from_impl<_database_t>(Check{}, table);
      }

    private:
      template <typename Database, typename Table>
      auto _from_impl(const std::false_type&, Table table) const -> bad_statement;

      template <typename Database, typename Table>
      auto _from_impl(const std::true_type&, Table table) const
          -> _new_statement_t<std::true_type, from_t<Database, from_table_t<Table>>>
      {
        return {static_cast<const derived_statement_t<Policies>&>(*this),
                from_data_t<Database, from_table_t<Table>>{from_table(table)}};
      }
    };
  };

  // Interpreters
  template <typename Context, typename Database, typename Table>
  struct serializer_t<Context, from_data_t<Database, Table>>
  {
    using _serialize_check = serialize_check_of<Context, Table>;
    using T = from_data_t<Database, Table>;

    static Context& _(const T& t, Context& context)
    {
      context << " FROM ";
      serialize(t._table, context);
      if (not t._dynamic_tables.empty())
      {
        interpret_list(t._dynamic_tables, "", context);
      }
      return context;
    }
  };

  template <typename T>
  auto from(T&& t) -> decltype(statement_t<void, no_from_t>().from(std::forward<T>(t)))
  {
    return statement_t<void, no_from_t>().from(std::forward<T>(t));
  }

  template <typename Database, typename T>
  auto dynamic_from(const Database&, T&& t)
      -> decltype(statement_t<Database, no_from_t>().dynamic_from(std::forward<T>(t)))
  {
    return statement_t<Database, no_from_t>().dynamic_from(std::forward<T>(t));
  }
}

#endif
