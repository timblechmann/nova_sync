/// Semaphore async handlers invoke with `expected<void, error_code>`.
/// Semaphore acquire completes synchronously; no lock guard returned (cf. events).
///
/// The generic async helpers from nova/sync/detail/async_support.hpp are used.
#include <nova/sync/detail/async_support.hpp>
#include <nova/sync/semaphore/concepts.hpp>

#if defined( NOVA_SYNC_HAS_EXPECTED )

#  include <system_error>

namespace nova::sync::detail {

template < typename Handler >
void invoke_semaphore_success( Handler&& handler )
{
    invoke_void_handler_success( std::forward< Handler >( handler ) );
}

template < typename Handler >
void invoke_semaphore_error( Handler&& handler, std::error_code ec )
{
    invoke_handler_error< void, Handler >( std::forward< Handler >( handler ), ec );
}

template < typename Handler >
void invoke_semaphore_error( Handler&& handler, std::errc ec )
{
    invoke_semaphore_error( std::forward< Handler >( handler ), std::make_error_code( ec ) );
}

template < typename Handler >
concept invocable_with_semaphore_expected = false
#  ifdef NOVA_SYNC_HAS_STD_EXPECTED
                                            || std::invocable< Handler, std::expected< void, std::error_code > >
#  endif
#  ifdef NOVA_SYNC_HAS_TL_EXPECTED
                                            || std::invocable< Handler, tl::expected< void, std::error_code > >
#  endif
    ;

} // namespace nova::sync::detail

#endif // defined( NOVA_SYNC_HAS_EXPECTED )
