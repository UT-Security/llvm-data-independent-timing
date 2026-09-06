<?php
/* Experiment 11: raise phpass to 2^16 rounds (Drupal 7/8's order of magnitude).
 * wp_hash_password is pluggable; mu-plugins load before pluggable.php, so this
 * definition wins. PasswordHash(11, true): phpass adds 5 for PHP >= 5, so the
 * stored hash carries count_log2 = 16 and CheckPassword runs 65,536 md5() calls. */
if ( ! function_exists( 'wp_hash_password' ) ) {
	function wp_hash_password( $password ) {
		global $wp_hasher;
		require_once ABSPATH . WPINC . '/class-phpass.php';
		$wp_hasher = new PasswordHash( 11, true );
		return $wp_hasher->HashPassword( trim( $password ) );
	}
}
