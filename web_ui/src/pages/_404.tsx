import { t } from '../i18n.js';

export function NotFound() {
	return (
		<section>
			<h1>{t('notFound.title')}</h1>
			<p>{t('notFound.message')}</p>
		</section>
	);
}
