import type { BaseLayoutProps } from 'fumadocs-ui/layouts/shared';
import { Cp3Logo } from '@/components/cp3-logo';

export function baseOptions(): BaseLayoutProps {
  return {
    nav: {
      title: <Cp3Logo className="h-5 w-auto shrink-0" />,
    },
    githubUrl: 'https://github.com/Krishnesh-Mishra/cp3',
  };
}
