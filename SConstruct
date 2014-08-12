import os

user_dirs = [ 'user_code/x_shadow1', 'user_code/x_shadow2' ]
user_libs = []

test_env = Environment( ENV = os.environ, CCFLAGS=[ '-DLEVEL_NORMAL', '-DLEVEL_TEST', '-g', '-Wall' ], LIBS=[ 'pthread', 'crypto', 'z', 'cares', 'json-c' ], MOON_TARGET='test', CPPPATH='#./moon_code/include_test', ANALYSE_DIRS=[ 'test_code' ] )
release_env = Environment( ENV = os.environ, CCFLAGS='-DLEVEL_NORMAL', LIBS=[ 'pthread', 'crypto', 'z', 'cares', 'json-c'  ], MOON_TARGET='release', CPPPATH='#./moon_code/include_release', ANALYSE_DIRS=user_dirs )

def move_head( source, target, env, for_signature):
	cmd = []
	min_len = len( source )
	if min_len > len( target ):
		min_len = len( target )
	for i in range( 0, min_len ):
		cmd.append( 'cp %s %s' % ( source[ i ], target[ i ] ) )
	return ';'.join( cmd )

bld_move_head = Builder( generator = move_head )
test_env.Append( BUILDERS = { 'Move_head' : bld_move_head } )
release_env.Append( BUILDERS = { 'Move_head' : bld_move_head } )

env = test_env.Clone();
Export( 'env' )
#env.Append( CCFLAGS = '-DMOON_ID=%s' % ( 'test_code' ) )
#test_lib = SConscript( './test_code/SConstruct' )
#env.Append( DEPEND_USER_CODE = test_lib )
SConscript( './test_shell/SConstruct' )
moon_lib = SConscript( './moon_code/SConstruct')
#env.Program( 'test', [ test_lib ] )

'''
if len( user_dirs ) > 0 :
	env = release_env.Clone()
	env.Append( CCFLAGS = [ '-DMOON_ID' ] )
	for dir in user_dirs :
		env = env.Clone()
		cflags = env['CCFLAGS'];
		for i in range( 0, len( cflags ) ) :
			if cflags[i].startswith( '-DMOON_ID' ) :
				cflags[i] = '-DMOON_ID=%s' % ( dir.split('/')[-1] ) 
		env.Replace( CCFLAGS = cflags )
		Export( 'env' )
		user_lib = SConscript( './%s/SConstruct' % ( dir ) )
		user_libs.append( user_lib )
	env = release_env.Clone();
	env.Append( DEPEND_USER_CODE = user_libs )
	Export( 'env' )
	moon_lib = SConscript( './moon_code/SConstruct' )
	for i in range( 0, len( user_libs ) ):
		env.Program( user_dirs[ i ].split('/')[-1], user_libs[ i ] )
#	env.Program( 'connect', [ user_libs, moon_lib ] )
'''
